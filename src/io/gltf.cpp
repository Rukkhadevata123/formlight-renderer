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

std::span<const std::byte> embedded_bytes(const fastgltf::DataSource &source)
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
    throw std::runtime_error("Expected embedded GLB buffer or image data");
}

const fastgltf::Accessor &geometry_accessor(const fastgltf::Asset &asset, std::size_t index)
{
    const auto &accessor = asset.accessors.at(index);
    if (accessor.sparse)
    {
        throw std::runtime_error("Sparse geometry accessors are not supported yet");
    }
    if (accessor.bufferViewIndex)
    {
        const auto &view = asset.bufferViews.at(*accessor.bufferViewIndex);
        const auto &data = asset.buffers.at(view.bufferIndex).data;
        embedded_bytes(data);
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

std::vector<Triangle> read_mesh(const fastgltf::Asset &asset, const fastgltf::Mesh &mesh, GltfContent content)
{
    std::vector<Triangle> triangles;
    for (const auto &primitive : mesh.primitives)
    {
        if (primitive.type != fastgltf::PrimitiveType::Triangles || !primitive.targets.empty())
        {
            throw std::runtime_error("Expected a static TRIANGLES primitive");
        }
        const auto position = primitive.findAttribute("POSITION");
        if (position == primitive.attributes.end())
        {
            throw std::runtime_error("Triangle primitive is missing POSITION");
        }
        const auto &positions = geometry_accessor(asset, position->accessorIndex);
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
        if (const auto normal = primitive.findAttribute("NORMAL"); normal != primitive.attributes.end())
        {
            const auto &normals = geometry_accessor(asset, normal->accessorIndex);
            if (normals.count != positions.count || normals.type != fastgltf::AccessorType::Vec3)
            {
                throw std::runtime_error("NORMAL must supply one VEC3 per position");
            }
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                asset, normals, [&](auto n, std::size_t i)
                { vertices[i].normal = unit_vector({n[0], n[1], n[2]}, "NORMAL must be finite and nonzero"); });
        }
        const auto texcoord = primitive.findAttribute("TEXCOORD_0");
        if (const auto color = primitive.findAttribute("COLOR_0");
            content == GltfContent::BaseColor && color != primitive.attributes.end())
        {
            read_vertex_colors(asset, geometry_accessor(asset, color->accessorIndex), vertices);
        }
        const auto material_slot = primitive.materialIndex ? *primitive.materialIndex + 1 : 0;
        if (content == GltfContent::BaseColor && texcoord == primitive.attributes.end() && primitive.materialIndex &&
            asset.materials.at(*primitive.materialIndex).pbrData.baseColorTexture)
        {
            throw std::runtime_error("Textured primitive is missing TEXCOORD_0");
        }
        if (texcoord != primitive.attributes.end())
        {
            const auto &uvs = geometry_accessor(asset, texcoord->accessorIndex);
            if (uvs.count != positions.count)
            {
                throw std::runtime_error("TEXCOORD_0 and POSITION have different vertex counts");
            }
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
                asset, uvs,
                [&](auto uv, std::size_t i)
                {
                    if (!std::isfinite(uv[0]) || !std::isfinite(uv[1]))
                    {
                        throw std::runtime_error("TEXCOORD_0 contains a non-finite coordinate");
                    }
                    vertices[i].uv = {uv[0], uv[1]};
                });
        }

        // GenerateMeshIndices also supplies consecutive indices for non-indexed primitives.
        const auto &indices = geometry_accessor(asset, primitive.indicesAccessor.value());
        using enum fastgltf::ComponentType;
        if (indices.type != fastgltf::AccessorType::Scalar || indices.normalized ||
            (indices.componentType != UnsignedByte && indices.componentType != UnsignedShort &&
             indices.componentType != UnsignedInt) ||
            indices.count % 3 != 0)
        {
            throw std::runtime_error("Expected unsigned triangle index triples");
        }
        if (indices.count / 3 > std::numeric_limits<std::uint32_t>::max() - triangles.size())
        {
            throw std::runtime_error("Too many triangles for mesh-local primitive IDs");
        }
        const auto start = triangles.size();
        triangles.resize(start + indices.count / 3);
        fastgltf::iterateAccessorWithIndex<std::uint32_t>(
            asset, indices,
            [&](std::uint32_t index, std::size_t i)
            {
                if (index >= vertices.size())
                {
                    throw std::runtime_error("Triangle index exceeds POSITION count");
                }
                const auto face = start + i / 3;
                auto &triangle = triangles[face];
                triangle.id = PrimitiveId{static_cast<std::uint32_t>(face)};
                triangle.material_index = material_slot;
                triangle.vertices[i % 3] = vertices[index];
            });
    }
    return triangles;
}

AffineTransform instance_transform(const fastgltf::math::nmat4x4 &world)
{
    const auto column = [&](std::size_t i) -> Vec3 { return {world[i][0], world[i][1], world[i][2]}; };
    return {Basis3{column(0), column(1), column(2)}, column(3)};
}

ColorImage read_image(const fastgltf::Asset &asset, std::size_t index)
{
    const auto &source = asset.images.at(index).data;
    if (const auto *reference = std::get_if<fastgltf::sources::BufferView>(&source))
    {
        const auto &view = asset.bufferViews.at(reference->bufferViewIndex);
        const auto bytes = embedded_bytes(asset.buffers.at(view.bufferIndex).data);
        if (view.byteOffset > bytes.size() || view.byteLength > bytes.size() - view.byteOffset)
        {
            throw std::runtime_error("Image buffer view exceeds its embedded buffer");
        }
        return decode_base_color(bytes.subspan(view.byteOffset, view.byteLength));
    }
    return decode_base_color(embedded_bytes(source));
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

BaseColorMaterial read_material(const fastgltf::Asset &asset, const fastgltf::Material &material,
                                std::vector<std::optional<ColorImage>> &images)
{
    const auto &pbr = material.pbrData;
    const auto &f = pbr.baseColorFactor;
    BaseColorMaterial result{.factor = {f[0], f[1], f[2], f[3]}, .texture = {}, .double_sided = material.doubleSided};
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
        if (info->texCoordIndex != 0)
        {
            throw std::runtime_error("Base-color preview requires TEXCOORD_0");
        }
        const auto &texture = asset.textures.at(info->textureIndex);
        const auto image = texture.imageIndex.value();
        if (!images.at(image))
        {
            images[image] = read_image(asset, image);
        }
        TextureSampler sampler;
        if (texture.samplerIndex)
        {
            const auto &source = asset.samplers.at(*texture.samplerIndex);
            sampler = {wrap_mode(source.wrapS), wrap_mode(source.wrapT),
                       source.magFilter == fastgltf::Filter::Nearest ? TexelFilter::Nearest : TexelFilter::Linear};
        }
        result.texture = BaseColorTexture{image, sampler};
    }
    return result;
}

std::vector<std::optional<BaseColorMaterial>> read_materials(const fastgltf::Asset &asset,
                                                             std::span<const std::vector<Triangle>> meshes,
                                                             std::vector<std::optional<ColorImage>> &images)
{
    std::vector<std::optional<BaseColorMaterial>> materials(1 + asset.materials.size());
    materials[0] = BaseColorMaterial{};
    for (const auto &mesh : meshes)
    {
        for (const auto &face : mesh)
        {
            auto &slot = materials.at(face.material_index);
            if (!slot)
            {
                slot = read_material(asset, asset.materials.at(face.material_index - 1), images);
            }
        }
    }
    return materials;
}
} // namespace

std::expected<GltfScene, std::string> GltfScene::load_glb(const std::filesystem::path &path, GltfContent content)
{
    try
    {
        auto file = fastgltf::GltfDataBuffer::FromPath(path);
        if (file.error() != fastgltf::Error::None)
        {
            throw std::runtime_error("Could not open or read GLB file");
        }
        fastgltf::Parser parser;
        auto parsed = parser.loadGltfBinary(file.get(), path.parent_path(), fastgltf::Options::GenerateMeshIndices);
        check(parsed.error());
        const auto &asset = parsed.get();
        check(fastgltf::validate(asset));
        const auto scene_index = asset.defaultScene.value_or(0);
        if (scene_index >= asset.scenes.size())
        {
            throw std::runtime_error("GLB has no valid scene to load");
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
                    result.meshes_.push_back(read_mesh(asset, asset.meshes.at(*node.meshIndex), content));
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
        if (content == GltfContent::BaseColor)
        {
            result.images_.resize(asset.images.size());
            result.materials_ = read_materials(asset, result.meshes_, result.images_);
        }
        return result;
    }
    catch (const std::exception &error)
    {
        return std::unexpected(std::format("{}: {}", path.string(), error.what()));
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
    materials_.emplace_back(BaseColorMaterial{color, {}});
    meshes_.push_back(std::move(mesh));
    node_instance_slots_.push_back(instances_.size());
    instances_.push_back({id, meshes_.back(), {}});
}

LinearColor GltfScene::sample_base_color(const BaseColorMaterial &material, const Triangle &face,
                                         Barycentrics weights) const
{
    const auto vertex = face.color_at(weights);
    const auto &factor = material.factor;
    LinearColor color{factor.r * vertex.r, factor.g * vertex.g, factor.b * vertex.b, factor.a * vertex.a};
    if (const auto &binding = material.texture)
    {
        const auto texel = sample_texture(images_[binding->image].value(), face.uv_at(weights), binding->sampler);
        color = {color.r * texel.r, color.g * texel.g, color.b * texel.b, color.a * texel.a};
    }
    return color;
}

LinearColor GltfScene::base_color(const Triangle &face, Barycentrics weights) const
{
    const auto &mat = material(face);
    auto color = sample_base_color(mat, face, weights);
    color.a = mat.alpha_mode == AlphaMode::Blend ? std::clamp(color.a, 0.0f, 1.0f) : 1.0f;
    return color;
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
               (mat.alpha_mode == AlphaMode::Mask && sample_base_color(mat, face, point.weights).a >= mat.alpha_cutoff);
    };
}
} // namespace formlight
