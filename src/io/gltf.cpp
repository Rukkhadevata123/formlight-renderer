module;

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

module formlight.gltf;

import std;
import formlight.image_codec;

namespace formlight
{
namespace
{
void check(fastgltf::Error error)
{
    if (error != fastgltf::Error::None)
    {
        throw std::runtime_error(std::string{fastgltf::getErrorMessage(error)});
    }
}

using Path = std::filesystem::path;

std::string path_text(const Path &path)
{
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

struct ModelResources
{
    fastgltf::Asset &asset;
    Path directory;
    // Byte views in the parsed asset borrow this storage until mesh and texture import finishes.
    std::map<Path, std::vector<std::byte>> files{};

    std::span<const std::byte> buffer_bytes(std::size_t index);
    const fastgltf::Accessor &geometry_accessor(std::size_t index);

    Path local_path(const fastgltf::sources::URI &source) const
    {
        const auto &uri = *source.uri;
        const auto relative = uri.fspath();
        if (!uri.scheme().empty() || !uri.host().empty() || relative.empty() || relative.has_root_path() ||
            relative.native().find(Path::value_type{}) != Path::string_type::npos)
        {
            return {};
        }
        return (directory / relative).lexically_normal();
    }

    Path require_path(const fastgltf::sources::URI &source) const
    {
        auto path = local_path(source);
        if (path.empty())
        {
            throw std::runtime_error(std::format("Expected a relative file URI: {}", source.uri->string()));
        }
        return path;
    }

    std::span<const std::byte> read(const Path &path)
    {
        if (const auto it = files.find(path); it != files.end())
        {
            return it->second;
        }
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        const std::streamoff length = file.tellg();
        if (length < 0 || !std::in_range<std::size_t>(length))
        {
            throw std::runtime_error(std::format("Cannot open or size external resource: {}", path_text(path)));
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(length));
        if (!file.seekg(0) || !file.read(reinterpret_cast<char *>(bytes.data()), length))
        {
            throw std::runtime_error(std::format("Cannot read external resource: {}", path_text(path)));
        }
        return files.emplace(path, std::move(bytes)).first->second;
    }
};

std::span<const std::byte> loaded_bytes(const fastgltf::DataSource &source)
{
    if (const auto *a = std::get_if<fastgltf::sources::Array>(&source))
    {
        return {a->bytes.data(), a->bytes.size_bytes()};
    }
    if (const auto *v = std::get_if<fastgltf::sources::Vector>(&source))
    {
        return v->bytes;
    }
    if (const auto *v = std::get_if<fastgltf::sources::ByteView>(&source))
    {
        return v->bytes;
    }
    throw std::runtime_error("Expected glTF buffer or image bytes");
}

std::span<const std::byte> ModelResources::buffer_bytes(std::size_t index)
{
    auto &buffer = asset.buffers.at(index);
    if (const auto *uri = std::get_if<fastgltf::sources::URI>(&buffer.data))
    {
        const auto path = require_path(*uri);
        const auto bytes = read(path);
        if (bytes.size() < buffer.byteLength)
        {
            throw std::runtime_error(std::format("{}: buffer declares {} bytes, file contains {}", path_text(path),
                                                 buffer.byteLength, bytes.size()));
        }
        buffer.data = fastgltf::sources::ByteView{bytes.first(buffer.byteLength), uri->mimeType};
    }
    return loaded_bytes(buffer.data);
}

const fastgltf::Accessor &ModelResources::geometry_accessor(std::size_t index)
{
    const auto &accessor = asset.accessors.at(index);
    if (accessor.sparse)
    {
        throw std::runtime_error("Sparse geometry accessors are not supported yet");
    }
    if (accessor.bufferViewIndex)
    {
        const auto &view = asset.bufferViews.at(*accessor.bufferViewIndex);
        buffer_bytes(view.bufferIndex);
    }
    return accessor;
}

void read_vertex_colors(const fastgltf::Asset &asset, const fastgltf::Accessor &colors,
                        std::span<TriangleVertex> vertices)
{
    if (colors.count != vertices.size())
    {
        throw std::runtime_error("COLOR_0 and POSITION have different vertex counts");
    }
    if (colors.type == fastgltf::AccessorType::Vec3)
    {
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, colors, [&](auto rgb, std::size_t i)
                                                                  { vertices[i].color = {rgb[0], rgb[1], rgb[2], 1}; });
    }
    else if (colors.type == fastgltf::AccessorType::Vec4)
    {
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
            asset, colors, [&](auto rgba, std::size_t i) { vertices[i].color = {rgba[0], rgba[1], rgba[2], rgba[3]}; });
    }
    else
    {
        throw std::runtime_error("COLOR_0 must supply VEC3 or VEC4 colors");
    }
}

void read_texcoords(ModelResources &resources, const fastgltf::Primitive &primitive, std::span<TriangleVertex> vertices,
                    GltfContent content, std::size_t mesh_index, std::size_t primitive_index)
{
    const auto &asset = resources.asset;
    constexpr std::array<std::string_view, 2> names{"TEXCOORD_0", "TEXCOORD_1"};
    std::array<bool, names.size()> present{};
    for (std::size_t set = 0; set < names.size(); ++set)
    {
        const auto name = names[set];
        const auto attribute = primitive.findAttribute(name);
        if (attribute == primitive.attributes.end())
        {
            continue;
        }
        present[set] = true;
        const auto &uvs = resources.geometry_accessor(attribute->accessorIndex);
        if (uvs.count != vertices.size() || uvs.type != fastgltf::AccessorType::Vec2)
        {
            throw std::runtime_error(std::format("Mesh {} primitive {}: {} must supply one VEC2 per position",
                                                 mesh_index, primitive_index, name));
        }
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
            asset, uvs,
            [&](auto uv, std::size_t i)
            {
                if (!std::isfinite(uv[0]) || !std::isfinite(uv[1]))
                {
                    throw std::runtime_error(std::format("Mesh {} primitive {}: {} contains a non-finite coordinate",
                                                         mesh_index, primitive_index, name));
                }
                vertices[i].texcoords[set] = {uv[0], uv[1]};
            });
    }
    if (content == GltfContent::Materials && primitive.materialIndex)
    {
        const auto &material = asset.materials.at(*primitive.materialIndex);
        const auto require = [&](std::string_view slot, const auto &binding)
        {
            if (!binding)
            {
                return;
            }
            const auto set = binding->texCoordIndex;
            if (set >= present.size() || !present[set])
            {
                throw std::runtime_error(std::format("Mesh {} primitive {} material {}: {} requires {} TEXCOORD_{}",
                                                     mesh_index, primitive_index, *primitive.materialIndex, slot,
                                                     set >= present.size() ? "unsupported" : "missing", set));
            }
        };
        require("baseColorTexture", material.pbrData.baseColorTexture);
        require("metallicRoughnessTexture", material.pbrData.metallicRoughnessTexture);
        require("normalTexture", material.normalTexture);
        require("occlusionTexture", material.occlusionTexture);
        require("emissiveTexture", material.emissiveTexture);
    }
}

// Offsets into the primitive's index stream, in the winding order of the expanded triangle.
std::array<std::size_t, 3> triangle_corners(fastgltf::PrimitiveType mode, std::size_t face)
{
    using enum fastgltf::PrimitiveType;
    switch (mode)
    {
    case Triangles:
        return {3 * face, 3 * face + 1, 3 * face + 2};
    case TriangleFan:
        return {0, face + 1, face + 2};
    case TriangleStrip:
        if (face % 2 == 0)
        {
            return {face, face + 1, face + 2};
        }
        return {face + 1, face, face + 2};
    default:
        throw std::runtime_error("Expected TRIANGLES, TRIANGLE_STRIP or TRIANGLE_FAN");
    }
}

void append_triangles(std::vector<Triangle> &triangles, ModelResources &resources, const fastgltf::Primitive &primitive,
                      std::span<const TriangleVertex> vertices)
{
    // GenerateMeshIndices supplies consecutive indices for non-indexed primitives.
    const auto &accessor = resources.geometry_accessor(primitive.indicesAccessor.value());
    using enum fastgltf::ComponentType;
    if (accessor.type != fastgltf::AccessorType::Scalar || accessor.normalized ||
        (accessor.componentType != UnsignedByte && accessor.componentType != UnsignedShort &&
         accessor.componentType != UnsignedInt))
    {
        throw std::runtime_error("Expected unsigned scalar triangle indices");
    }
    using enum fastgltf::PrimitiveType;
    const auto count = accessor.count;
    if (primitive.type == Triangles && count % 3 != 0)
    {
        throw std::runtime_error("TRIANGLES index count must be a multiple of three");
    }
    const auto face_count = primitive.type == Triangles ? count / 3 : std::max(count, std::size_t{2}) - 2;
    if (face_count > std::numeric_limits<std::uint32_t>::max() - triangles.size())
    {
        throw std::runtime_error("Too many triangles for mesh-local primitive IDs");
    }

    std::vector<std::uint32_t> indices(count);
    const auto read_index = [&](std::uint32_t index, std::size_t i)
    {
        if (index >= vertices.size())
        {
            throw std::runtime_error("Triangle index exceeds POSITION count");
        }
        indices[i] = index;
    };
    fastgltf::iterateAccessorWithIndex<std::uint32_t>(resources.asset, accessor, read_index);

    const auto material_slot = primitive.materialIndex ? *primitive.materialIndex + 1 : 0;
    const auto start = triangles.size();
    triangles.resize(start + face_count);
    for (std::size_t face = 0; face < face_count; ++face)
    {
        const auto [a, b, c] = triangle_corners(primitive.type, face);
        triangles[start + face] = {
            .id = PrimitiveId{static_cast<std::uint32_t>(start + face)},
            .vertices = {vertices[indices[a]], vertices[indices[b]], vertices[indices[c]]},
            .material_index = material_slot,
        };
    }
}

std::vector<Triangle> read_mesh(ModelResources &resources, std::size_t mesh_index, GltfContent content)
{
    const auto &asset = resources.asset;
    const auto &mesh = asset.meshes.at(mesh_index);
    std::vector<Triangle> triangles;
    for (std::size_t primitive_index = 0; primitive_index < mesh.primitives.size(); ++primitive_index)
    {
        const auto &primitive = mesh.primitives[primitive_index];
        using enum fastgltf::PrimitiveType;
        if (primitive.type != Triangles && primitive.type != TriangleStrip && primitive.type != TriangleFan)
        {
            throw std::runtime_error("Expected TRIANGLES, TRIANGLE_STRIP or TRIANGLE_FAN");
        }
        if (!primitive.targets.empty())
        {
            throw std::runtime_error("Morph targets are not supported");
        }
        const auto position = primitive.findAttribute("POSITION");
        if (position == primitive.attributes.end())
        {
            throw std::runtime_error("Triangle primitive is missing POSITION");
        }
        const auto &positions = resources.geometry_accessor(position->accessorIndex);
        std::vector<TriangleVertex> vertices(positions.count);
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
            asset, positions,
            [&](auto p, std::size_t i)
            {
                if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]))
                {
                    throw std::runtime_error("POSITION contains a non-finite coordinate");
                }
                vertices[i].position = {p[0], p[1], p[2]};
            });
        const auto normal = primitive.findAttribute("NORMAL");
        if (normal != primitive.attributes.end())
        {
            const auto &normals = resources.geometry_accessor(normal->accessorIndex);
            if (normals.count != positions.count || normals.type != fastgltf::AccessorType::Vec3)
            {
                throw std::runtime_error("NORMAL must supply one VEC3 per position");
            }
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                asset, normals, [&](auto n, std::size_t i)
                { vertices[i].normal = unit_vector({n[0], n[1], n[2]}, "NORMAL must be finite and nonzero"); });
        }
        read_texcoords(resources, primitive, vertices, content, mesh_index, primitive_index);
        const auto tangent = primitive.findAttribute("TANGENT");
        const bool use_tangents = content == GltfContent::Materials && normal != primitive.attributes.end() &&
                                  tangent != primitive.attributes.end();
        if (use_tangents)
        {
            const auto &accessor = resources.geometry_accessor(tangent->accessorIndex);
            if (accessor.type != fastgltf::AccessorType::Vec4 || accessor.count != vertices.size())
            {
                throw std::runtime_error("TANGENT must supply one VEC4 per position");
            }
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
                asset, accessor,
                [&](auto t, std::size_t i)
                {
                    if (t[3] != 1 && t[3] != -1)
                    {
                        throw std::runtime_error("TANGENT.w must be +1 or -1");
                    }
                    vertices[i].tangent = unit_vector({t[0], t[1], t[2]}, "TANGENT must be finite and nonzero");
                    vertices[i].tangent_sign = t[3];
                });
        }
        if (const auto color = primitive.findAttribute("COLOR_0");
            content == GltfContent::Materials && color != primitive.attributes.end())
        {
            read_vertex_colors(asset, resources.geometry_accessor(color->accessorIndex), vertices);
        }
        const auto start = triangles.size();
        append_triangles(triangles, resources, primitive, vertices);
        if (content != GltfContent::Materials || use_tangents || !primitive.materialIndex)
        {
            continue;
        }
        if (const auto &map = asset.materials.at(*primitive.materialIndex).normalTexture)
        {
            generate_tangents(std::span{triangles}.subspan(start), map->texCoordIndex);
        }
    }
    return triangles;
}

AffineTransform instance_transform(const fastgltf::math::nmat4x4 &world)
{
    const auto column = [&](std::size_t i) -> Vec3 { return {world[i][0], world[i][1], world[i][2]}; };
    return {Basis3{column(0), column(1), column(2)}, column(3)};
}

ColorImage read_image(ModelResources &resources, std::size_t index, RgbEncoding encoding)
{
    const auto &asset = resources.asset;
    const auto &source = asset.images.at(index).data;
    if (const auto *reference = std::get_if<fastgltf::sources::BufferView>(&source))
    {
        const auto &view = asset.bufferViews.at(reference->bufferViewIndex);
        const auto bytes = resources.buffer_bytes(view.bufferIndex);
        if (view.byteOffset > bytes.size() || view.byteLength > bytes.size() - view.byteOffset)
        {
            throw std::runtime_error("Image buffer view exceeds its buffer");
        }
        return decode_image(bytes.subspan(view.byteOffset, view.byteLength), encoding);
    }
    if (const auto *uri = std::get_if<fastgltf::sources::URI>(&source))
    {
        const auto path = resources.require_path(*uri);
        const auto bytes = resources.read(path);
        try
        {
            return decode_image(bytes, encoding);
        }
        catch (const std::runtime_error &error)
        {
            throw std::runtime_error(std::format("{}: {}", path_text(path), error.what()));
        }
    }
    return decode_image(loaded_bytes(source), encoding);
}

WrapMode wrap_mode(fastgltf::Wrap wrap)
{
    switch (wrap)
    {
    case fastgltf::Wrap::Repeat:
        return WrapMode::Repeat;
    case fastgltf::Wrap::MirroredRepeat:
        return WrapMode::MirroredRepeat;
    case fastgltf::Wrap::ClampToEdge:
        return WrapMode::ClampToEdge;
    default:
        throw std::runtime_error("Invalid glTF texture wrap mode");
    }
}

TextureSampler read_sampler(const fastgltf::Sampler &source)
{
    using enum fastgltf::Filter;
    const auto u = wrap_mode(source.wrapS), v = wrap_mode(source.wrapT);
    const auto mag = source.magFilter == Nearest ? TexelFilter::Nearest : TexelFilter::Linear;
    switch (source.minFilter.value_or(Linear))
    {
    case Nearest:
        return {u, v, mag, TexelFilter::Nearest};
    case Linear:
        return {u, v, mag, TexelFilter::Linear};
    case NearestMipMapNearest:
        return {u, v, mag, TexelFilter::Nearest, MipFilter::Nearest};
    case LinearMipMapNearest:
        return {u, v, mag, TexelFilter::Linear, MipFilter::Nearest};
    case NearestMipMapLinear:
        return {u, v, mag, TexelFilter::Nearest, MipFilter::Linear};
    case LinearMipMapLinear:
        return {u, v, mag, TexelFilter::Linear, MipFilter::Linear};
    }
    throw std::runtime_error("Invalid glTF minification filter");
}

struct TextureLoader
{
    ModelResources &resources;
    std::vector<TextureImage> &images;
    std::map<std::pair<std::size_t, TextureUsage>, std::size_t> image_slots{};

    TextureBinding bind(const fastgltf::TextureInfo &info, TextureUsage usage);
};

TextureBinding TextureLoader::bind(const fastgltf::TextureInfo &info, TextureUsage usage)
{
    const auto &asset = resources.asset;
    const auto &texture = asset.textures.at(info.textureIndex);
    const auto image = texture.imageIndex.value();
    const auto [slot, inserted] = image_slots.try_emplace(std::pair{image, usage}, images.size());
    if (inserted)
    {
        const auto encoding = usage == TextureUsage::Color ? RgbEncoding::Srgb : RgbEncoding::Linear;
        images.emplace_back(read_image(resources, image, encoding), usage);
    }
    TextureSampler sampler;
    if (texture.samplerIndex)
    {
        sampler = read_sampler(asset.samplers.at(*texture.samplerIndex));
    }
    if (sampler.mip != MipFilter::None)
    {
        images[slot->second].generate_mipmaps();
    }
    return {slot->second, sampler, info.texCoordIndex};
}

GltfMaterial read_material(const fastgltf::Material &material, TextureLoader &textures)
{
    const auto &pbr = material.pbrData;
    const auto &f = pbr.baseColorFactor;
    const auto &e = material.emissiveFactor;
    if (!(pbr.metallicFactor >= 0 && pbr.metallicFactor <= 1 && pbr.roughnessFactor >= 0 && pbr.roughnessFactor <= 1))
    {
        throw std::runtime_error("metallicFactor and roughnessFactor must be in [0, 1]");
    }
    if (!std::isfinite(material.emissiveStrength) || material.emissiveStrength < 0)
    {
        throw std::runtime_error("emissiveStrength must be finite and nonnegative");
    }
    GltfMaterial result{.base_color_factor = {f[0], f[1], f[2], f[3]},
                        .metallic_factor = pbr.metallicFactor,
                        .roughness_factor = pbr.roughnessFactor,
                        .emission_multiplier = scale_rgb({e[0], e[1], e[2]}, material.emissiveStrength),
                        .double_sided = material.doubleSided};
    if (material.alphaMode == fastgltf::AlphaMode::Mask)
    {
        result.alpha_mode = AlphaMode::Mask;
        result.alpha_cutoff = material.alphaCutoff;
    }
    else if (material.alphaMode == fastgltf::AlphaMode::Blend)
    {
        result.alpha_mode = AlphaMode::Blend;
    }
    if (const auto &info = pbr.baseColorTexture)
    {
        result.base_color_texture = textures.bind(*info, TextureUsage::Color);
    }
    if (const auto &info = pbr.metallicRoughnessTexture)
    {
        result.metallic_roughness_texture = textures.bind(*info, TextureUsage::Data);
    }
    if (const auto &info = material.normalTexture)
    {
        if (!std::isfinite(info->scale))
        {
            throw std::runtime_error("normalTexture.scale must be finite");
        }
        result.normal_texture = textures.bind(*info, TextureUsage::Normal);
        result.normal_scale = info->scale;
    }
    if (const auto &info = material.occlusionTexture)
    {
        if (!(info->strength >= 0 && info->strength <= 1))
        {
            throw std::runtime_error("occlusionTexture.strength must be in [0, 1]");
        }
        result.occlusion_texture = textures.bind(*info, TextureUsage::Data);
        result.occlusion_strength = info->strength;
    }
    if (const auto &info = material.emissiveTexture)
    {
        result.emissive_texture = textures.bind(*info, TextureUsage::Color);
    }
    return result;
}

std::vector<std::optional<GltfMaterial>> read_materials(ModelResources &resources,
                                                        std::span<const std::vector<Triangle>> meshes,
                                                        std::vector<TextureImage> &images)
{
    const auto &asset = resources.asset;
    TextureLoader textures{resources, images};
    std::vector<std::optional<GltfMaterial>> materials(1 + asset.materials.size());
    materials[0] = GltfMaterial{};
    for (const auto &mesh : meshes)
    {
        for (const auto &face : mesh)
        {
            auto &slot = materials.at(face.material_index);
            if (!slot)
            {
                slot = read_material(asset.materials.at(face.material_index - 1), textures);
            }
        }
    }
    return materials;
}
} // namespace

std::expected<GltfScene, std::string> GltfScene::load(const std::filesystem::path &path, GltfContent content)
{
    try
    {
        auto file = fastgltf::GltfDataBuffer::FromPath(path);
        if (file.error() != fastgltf::Error::None)
        {
            throw std::runtime_error("Could not open or read glTF model");
        }
        fastgltf::Parser parser{fastgltf::Extensions::KHR_materials_emissive_strength};
        const auto model = std::filesystem::absolute(path);
        auto parsed = parser.loadGltf(file.get(), model.parent_path(), fastgltf::Options::GenerateMeshIndices);
        check(parsed.error());
        auto &asset = parsed.get();
        check(fastgltf::validate(asset));
        ModelResources resources{asset, model.parent_path()};
        const auto scene_index = asset.defaultScene.value_or(0);
        if (scene_index >= asset.scenes.size())
        {
            throw std::runtime_error("glTF model has no valid scene to load");
        }
        const auto &scene = asset.scenes[scene_index];

        struct PendingNode
        {
            std::size_t index;
            fastgltf::math::nmat4x4 parent;
        };
        std::vector<PendingNode> pending;
        for (const auto root : scene.nodeIndices | std::views::reverse)
        {
            pending.push_back({root, fastgltf::math::nmat4x4{}});
        }
        GltfScene result;
        result.source_files_.push_back(model);
        // Protect referenced files even when only another scene or material in this asset uses them.
        const auto remember_files = [&](const auto &entries)
        {
            for (const auto &entry : entries)
            {
                if (const auto *uri = std::get_if<fastgltf::sources::URI>(&entry.data))
                {
                    if (auto file_path = resources.local_path(*uri); !file_path.empty())
                    {
                        result.source_files_.push_back(std::move(file_path));
                    }
                }
            }
        };
        remember_files(asset.buffers);
        remember_files(asset.images);
        result.node_instance_slots_.resize(asset.nodes.size());
        std::vector<std::optional<std::size_t>> mesh_slots(asset.meshes.size());
        std::vector<bool> visited(asset.nodes.size());
        while (!pending.empty())
        {
            const auto [index, parent] = pending.back();
            pending.pop_back();
            const auto &node = asset.nodes.at(index);
            if (visited[index])
            {
                throw std::runtime_error("Scene node appears more than once in the hierarchy");
            }
            visited[index] = true;
            const auto world = fastgltf::getTransformMatrix(node, parent);
            if (node.meshIndex)
            {
                if (node.skinIndex || !std::in_range<std::uint32_t>(index))
                {
                    throw std::runtime_error("Expected a static mesh node with a representable instance ID");
                }
                auto &slot = mesh_slots.at(*node.meshIndex);
                if (!slot)
                {
                    slot = result.meshes_.size();
                    result.meshes_.push_back(read_mesh(resources, *node.meshIndex, content));
                }
                result.node_instance_slots_[index] = result.instances_.size();
                result.instances_.push_back(
                    {InstanceId{static_cast<std::uint32_t>(index)}, result.meshes_[*slot], instance_transform(world)});
            }
            for (const auto child : node.children | std::views::reverse)
            {
                pending.push_back({child, world});
            }
        }
        if (content == GltfContent::Materials)
        {
            result.materials_ = read_materials(resources, result.meshes_, result.images_);
        }
        return result;
    }
    catch (const std::exception &error)
    {
        return std::unexpected(std::format("{}: {}", path_text(path), error.what()));
    }
}

void GltfScene::check_output_path(const std::filesystem::path &path) const
{
    if (!std::filesystem::exists(path))
    {
        return;
    }
    for (const auto &source : source_files_)
    {
        if (std::filesystem::exists(source) && std::filesystem::equivalent(path, source))
        {
            throw std::invalid_argument(std::format("Output would overwrite a model resource: {}", path_text(source)));
        }
    }
}

void GltfScene::add_solid_mesh(std::vector<Triangle> mesh, LinearColor color)
{
    if (materials_.empty() || mesh.empty())
    {
        throw std::invalid_argument("Adding a solid mesh requires triangles and a scene with materials");
    }
    if (!std::in_range<std::uint32_t>(node_instance_slots_.size()) || !std::in_range<std::uint32_t>(mesh.size() - 1))
    {
        throw std::length_error("Mesh exceeds the instance or primitive ID range");
    }
    const InstanceId id{static_cast<std::uint32_t>(node_instance_slots_.size())};
    for (std::size_t i = 0; i < mesh.size(); ++i)
    {
        mesh[i].id = PrimitiveId{static_cast<std::uint32_t>(i)};
        mesh[i].material_index = materials_.size();
    }
    // Reserve all four slots before publishing the new material, mesh and instance.
    materials_.reserve(materials_.size() + 1);
    meshes_.reserve(meshes_.size() + 1);
    node_instance_slots_.reserve(node_instance_slots_.size() + 1);
    instances_.reserve(instances_.size() + 1);
    materials_.emplace_back(GltfMaterial{.base_color_factor = color, .metallic_factor = 0});
    meshes_.push_back(std::move(mesh));
    node_instance_slots_.push_back(instances_.size());
    instances_.push_back({id, meshes_.back(), {}});
}

LinearColor GltfScene::sample(const TextureBinding &binding, const Triangle &face, Barycentrics weights,
                              WeightGradients gradients) const
{
    const auto set = binding.texcoord_set;
    return images_[binding.image].sample(face.uv_at(weights, set), binding.sampler, face.uv_gradients(gradients, set));
}

LinearColor GltfScene::sample_base_color(const GltfMaterial &material, const Triangle &face, Barycentrics weights,
                                         WeightGradients gradients) const
{
    const auto vertex = face.color_at(weights);
    const auto &factor = material.base_color_factor;
    LinearColor color{factor.r * vertex.r, factor.g * vertex.g, factor.b * vertex.b, factor.a * vertex.a};
    if (const auto &binding = material.base_color_texture)
    {
        const auto texel = sample(*binding, face, weights, gradients);
        color = {color.r * texel.r, color.g * texel.g, color.b * texel.b, color.a * texel.a};
    }
    return color;
}

SurfaceMaterial GltfScene::surface_material(const Triangle &face, Barycentrics weights, WeightGradients gradients) const
{
    const auto &mat = material(face);
    auto color = sample_base_color(mat, face, weights, gradients);
    color.a = mat.alpha_mode == AlphaMode::Blend ? std::clamp(color.a, 0.0f, 1.0f) : 1.0f;
    auto emission = mat.emission_multiplier;
    if (color.a > 0 && mat.emissive_texture)
    {
        const auto texel = sample(*mat.emissive_texture, face, weights, gradients);
        emission = {emission.r * texel.r, emission.g * texel.g, emission.b * texel.b};
    }
    double metallic = mat.metallic_factor;
    double roughness = mat.roughness_factor;
    if (const auto &binding = mat.metallic_roughness_texture)
    {
        const auto texel = sample(*binding, face, weights, gradients);
        metallic *= texel.b;
        roughness *= texel.g;
    }
    double occlusion = 1;
    if (const auto &binding = mat.occlusion_texture)
    {
        const auto texel = sample(*binding, face, weights, gradients);
        occlusion = std::lerp(1.0, static_cast<double>(texel.r), mat.occlusion_strength);
    }
    return {color, emission, metallic, roughness, occlusion};
}

Vec3 GltfScene::shading_normal(const MeshInstance &instance, const Triangle &face, Barycentrics weights,
                               WeightGradients gradients, bool back_face) const
{
    const auto n = instance.transform.to_world_normal(face.normal_at(weights));
    const double side = back_face ? -1 : 1;
    const auto &mat = material(face);
    if (!mat.normal_texture)
    {
        return n * side;
    }

    const auto &[a, b, c] = face.vertices;
    const auto tangent =
        instance.transform.to_world_vector(a.tangent * weights.a + b.tangent * weights.b + c.tangent * weights.c);
    const auto orthogonal = tangent - n * dot(n, tangent);
    const double length = std::hypot(orthogonal.x, orthogonal.y, orthogonal.z);
    if (!(length > 0))
    {
        return n * side;
    }
    const auto t = orthogonal / length;
    const auto bitangent = cross(n, t) * (a.tangent_sign * instance.transform.handedness());
    const auto texel = sample(*mat.normal_texture, face, weights, gradients);
    const Vec3 mapped = t * ((2.0 * texel.r - 1) * mat.normal_scale) +
                        bitangent * ((2.0 * texel.g - 1) * mat.normal_scale) + n * (2.0 * texel.b - 1);
    const double magnitude = std::hypot(mapped.x, mapped.y, mapped.z);
    return (magnitude > 0 ? mapped / magnitude : n) * side;
}

SurfaceFilter GltfScene::solid_filter() const &
{
    if (std::ranges::none_of(materials_, [](const auto &m) { return m && m->alpha_mode != AlphaMode::Opaque; }))
    {
        return {};
    }
    return [this](const TrianglePoint &point)
    {
        const auto &face = instance(point.id.instance).triangles[std::to_underlying(point.id.primitive)];
        const auto &mat = material(face);
        return mat.alpha_mode == AlphaMode::Opaque ||
               (mat.alpha_mode == AlphaMode::Mask &&
                sample_base_color(mat, face, point.weights, point.gradients).a >= mat.alpha_cutoff);
    };
}
} // namespace formlight
