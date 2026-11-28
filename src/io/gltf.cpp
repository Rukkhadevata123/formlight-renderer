module;

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

module formlight.gltf;

import std;
import formlight.image_codec;
import formlight.gltf_resources;
import formlight.gltf_mesh;
import formlight.gltf_texture_transform;

namespace formlight
{
namespace
{
using namespace gltf_detail;

void check(fastgltf::Error error)
{
    if (error != fastgltf::Error::None)
    {
        throw std::runtime_error(std::string{fastgltf::getErrorMessage(error)});
    }
}

AffineTransform instance_transform(const fastgltf::math::nmat4x4 &world)
{
    const auto column = [&](std::size_t i) -> Vec3 { return {world[i][0], world[i][1], world[i][2]}; };
    return {Basis3{column(0), column(1), column(2)}, column(3)};
}

ColorImage decode_external_image(const Path &path, std::span<const std::byte> bytes, RgbEncoding encoding)
{
    try
    {
        return decode_image(bytes, encoding);
    }
    catch (const std::runtime_error &error)
    {
        throw std::runtime_error(std::format("{}: {}", path_text(path), error.what()));
    }
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
        return decode_external_image(path, resources.read(path), encoding);
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
    return {slot->second, sampler, texcoord_set(info), read_uv_transform(info)};
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
    if (const auto &transmission = material.transmission)
    {
        const auto factor = transmission->transmissionFactor;
        if (!(factor >= 0 && factor <= 1))
        {
            throw std::runtime_error("transmissionFactor must be in [0, 1]");
        }
        result.transmission_factor = factor;
        if (const auto &info = transmission->transmissionTexture)
        {
            result.transmission_texture = textures.bind(*info, TextureUsage::Data);
        }
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
        fastgltf::Parser parser{fastgltf::Extensions::KHR_materials_emissive_strength |
                                fastgltf::Extensions::KHR_materials_transmission |
                                fastgltf::Extensions::KHR_texture_transform};
        const auto model = std::filesystem::absolute(path);
        auto parsed = parser.loadGltf(file.get(), model.parent_path(), fastgltf::Options::GenerateMeshIndices);
        check(parsed.error());
        auto &asset = parsed.get();
        check(fastgltf::validate(asset));
        if (content == GltfContent::Materials &&
            std::ranges::contains(asset.extensionsRequired, fastgltf::extensions::KHR_materials_transmission))
        {
            throw std::runtime_error("This model requires KHR_materials_transmission shading");
        }
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
    const auto check_source = [&](const Path &source)
    {
        if (std::filesystem::exists(source) && std::filesystem::equivalent(path, source))
        {
            throw std::invalid_argument(std::format("Output would overwrite a model resource: {}", path_text(source)));
        }
    };
    std::ranges::for_each(source_files_, check_source);
    std::ranges::for_each(sphere_image_slots_ | std::views::keys, check_source);
}

GltfScene GltfScene::empty_scene()
{
    GltfScene result;
    result.materials_.emplace_back(GltfMaterial{});
    return result;
}

void GltfScene::add_spheres(std::span<const SphereSettings> additions)
{
    if (additions.empty())
    {
        return;
    }
    auto slots = sphere_image_slots_;
    std::vector<TextureImage> textures;
    std::vector<MarbleTexture> marbles;
    std::vector<CheckerTexture> checkers;
    std::vector<GltfMaterial> materials;
    materials.reserve(additions.size());
    for (const auto &sphere : additions)
    {
        (void)sphere.shape.bounds();
        sphere.validate_textures();
        const auto a = sphere.albedo;
        auto &material = materials.emplace_back(GltfMaterial{
            .base_color_factor = {static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z), 1},
            .metallic_factor = 0,
            .roughness_factor = sphere.type == PathSurface::Diffuse ? 1.0 : 0.0,
            .path_surface = sphere.type,
            .ior = sphere.ior,
            .fuzz = sphere.fuzz});
        if (sphere.checker)
        {
            material.checker_texture = checker_textures_.size() + checkers.size();
            checkers.push_back(*sphere.checker);
        }
        if (sphere.marble)
        {
            material.marble_texture = marble_textures_.size() + marbles.size();
            marbles.push_back({PerlinNoise{sphere.marble->seed}, sphere.marble->scale});
        }
        if (!sphere.albedo_texture.empty())
        {
            const auto path = std::filesystem::absolute(sphere.albedo_texture).lexically_normal();
            const auto [slot, inserted] = slots.try_emplace(path, images_.size() + textures.size());
            if (inserted)
            {
                auto image = decode_external_image(path, read_resource_bytes(path), RgbEncoding::Srgb);
                textures.emplace_back(std::move(image), TextureUsage::Color);
            }
            material.base_color_texture =
                TextureBinding{slot->second, {.u = WrapMode::Repeat, .v = WrapMode::ClampToEdge}};
        }
    }

    // Publish only after every image was decoded, so a failed batch can be retried on the same scene.
    reserve_sphere_instances(additions.size());
    materials_.reserve(materials_.size() + additions.size());
    images_.reserve(images_.size() + textures.size());
    checker_textures_.reserve(checker_textures_.size() + checkers.size());
    marble_textures_.reserve(marble_textures_.size() + marbles.size());
    images_.append_range(textures | std::views::as_rvalue);
    marble_textures_.append_range(marbles);
    checker_textures_.append_range(checkers);
    sphere_image_slots_.swap(slots);
    for (std::size_t i = 0; i < additions.size(); ++i)
    {
        const auto &sphere = additions[i];
        materials_.emplace_back(materials[i]);
        append_sphere_instance(sphere.shape, materials_.size() - 1, sphere.sampling_target);
    }
}

void GltfScene::add_diffuse_spheres(const std::vector<Sphere> &shapes, Vec3 albedo)
{
    if (shapes.empty())
    {
        return;
    }
    if (!finite_nonnegative_components(albedo) || std::max({albedo.x, albedo.y, albedo.z}) > 1)
    {
        throw std::invalid_argument("Sphere albedo must be in [0, 1]");
    }
    for (const auto &shape : shapes)
    {
        (void)shape.bounds();
    }
    reserve_sphere_instances(shapes.size());
    const auto slot = materials_.size();
    materials_.emplace_back(
        GltfMaterial{.base_color_factor = {static_cast<float>(albedo.x), static_cast<float>(albedo.y),
                                           static_cast<float>(albedo.z), 1},
                     .metallic_factor = 0});
    for (const auto &shape : shapes)
    {
        append_sphere_instance(shape, slot, false);
    }
}

void GltfScene::reserve_sphere_instances(std::size_t count)
{
    if (!std::in_range<std::uint32_t>(node_instance_slots_.size() + count - 1))
    {
        throw std::length_error("Sphere exceeds the instance ID range");
    }
    spheres_.reserve(spheres_.size() + count);
    node_instance_slots_.reserve(node_instance_slots_.size() + count);
}

void GltfScene::append_sphere_instance(const Sphere &shape, std::size_t material_index, bool sampling_target)
{
    const InstanceId id{static_cast<std::uint32_t>(node_instance_slots_.size())};
    spheres_.push_back({id, shape, material_index, sampling_target});
    node_instance_slots_.push_back(std::nullopt);
}

Bounds3 GltfScene::bounds() const
{
    auto bounds = instances_.empty() ? Bounds3::empty() : world_bounds(instances_);
    for (const auto &sphere : spheres_)
    {
        const auto box = sphere.shape.bounds();
        bounds.include(box.low);
        bounds.include(box.high);
    }
    for (const auto &medium : media)
    {
        const auto box = medium.boundary.bounds();
        bounds.include(box.low);
        bounds.include(box.high);
    }
    if (!(bounds.radius() > 0 && std::isfinite(bounds.radius())))
    {
        throw std::invalid_argument("The scene has no finite spatial extent");
    }
    return bounds;
}

void GltfScene::set_path_material(std::size_t gltf_index, PathSurface type, double ior, double fuzz)
{
    // Slot zero belongs to primitives that omit the glTF material.
    if (materials_.empty() || gltf_index >= materials_.size() - 1 || !materials_[gltf_index + 1])
    {
        throw std::invalid_argument(std::format("Path material {} is not used in the loaded scene", gltf_index));
    }
    auto &mat = *materials_[gltf_index + 1];
    mat.path_surface = type;
    mat.ior = ior;
    mat.fuzz = fuzz;
}

void GltfScene::mark_sampling_target(std::uint32_t node_index)
{
    if (node_index >= node_instance_slots_.size() || !node_instance_slots_[node_index])
    {
        throw std::invalid_argument(std::format("Sampling target node {} has no mesh in the active scene", node_index));
    }
    instances_[*node_instance_slots_[node_index]].sampling_target = true;
}

void GltfScene::add_solid_mesh(std::vector<Triangle> mesh, LinearColor color, std::vector<AffineTransform> transforms)
{
    add_mesh(std::move(mesh), {.base_color_factor = color, .metallic_factor = 0, .double_sided = true},
             std::move(transforms));
}

void GltfScene::add_emissive_quad(Vec3 origin, Vec3 u, Vec3 v, LinearColor emission)
{
    const TriangleVertex a{origin}, b{origin + u}, c{origin + u + v}, d{origin + v};
    add_mesh({Triangle{.vertices = {a, b, c}}, Triangle{.vertices = {a, c, d}}},
             {.base_color_factor = {0, 0, 0, 1}, .metallic_factor = 0, .emission_multiplier = emission},
             {AffineTransform{}});
}

void GltfScene::add_mesh(std::vector<Triangle> mesh, GltfMaterial material, std::vector<AffineTransform> transforms)
{
    if (materials_.empty() || mesh.empty() || transforms.empty())
    {
        throw std::invalid_argument(
            "Adding a solid mesh requires triangles, instance transforms and a scene with materials");
    }
    if (!std::in_range<std::uint32_t>(node_instance_slots_.size() + transforms.size() - 1) ||
        !std::in_range<std::uint32_t>(mesh.size() - 1))
    {
        throw std::length_error("Mesh exceeds the instance or primitive ID range");
    }
    for (std::size_t i = 0; i < mesh.size(); ++i)
    {
        mesh[i].id = PrimitiveId{static_cast<std::uint32_t>(i)};
        mesh[i].material_index = materials_.size();
    }
    // Reserve all storage before publishing the mesh and its instances.
    materials_.reserve(materials_.size() + 1);
    meshes_.reserve(meshes_.size() + 1);
    node_instance_slots_.reserve(node_instance_slots_.size() + transforms.size());
    instances_.reserve(instances_.size() + transforms.size());
    materials_.emplace_back(material);
    meshes_.push_back(std::move(mesh));
    for (const auto &transform : transforms)
    {
        const InstanceId id{static_cast<std::uint32_t>(node_instance_slots_.size())};
        node_instance_slots_.push_back(instances_.size());
        instances_.push_back({id, meshes_.back(), transform});
    }
}

LinearColor GltfScene::sample(const TextureBinding &binding, const Triangle &face, Barycentrics weights,
                              WeightGradients gradients) const
{
    const auto set = binding.texcoord_set;
    auto uv = face.uv_at(weights, set);
    auto footprint = face.uv_gradients(gradients, set);
    if (const auto &transform = binding.transform)
    {
        uv = transform->point(uv);
        footprint = {transform->vector(footprint.dx), transform->vector(footprint.dy)};
    }
    return images_[binding.image].sample(uv, binding.sampler, footprint);
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

SurfaceMaterial GltfScene::surface_material(const SphereInstance &sphere, Vec3 outward_normal) const
{
    const auto &mat = material(sphere.material_index);
    auto color = mat.base_color_factor;
    if (const auto &binding = mat.base_color_texture)
    {
        const auto texel = images_[binding->image].sample(sphere_uv(outward_normal), binding->sampler);
        color.r *= texel.r;
        color.g *= texel.g;
        color.b *= texel.b;
    }
    // Both solid textures use the reference pose, so their patterns travel with a moving sphere.
    const Vec3 point = sphere.shape.center + outward_normal * sphere.shape.radius;
    if (mat.marble_texture)
    {
        color = scale_rgb(color, marble_textures_[*mat.marble_texture].sample(point));
    }
    if (mat.checker_texture)
    {
        const Vec3 tint = checker_textures_[*mat.checker_texture].sample(point);
        color.r *= static_cast<float>(tint.x);
        color.g *= static_cast<float>(tint.y);
        color.b *= static_cast<float>(tint.z);
    }
    return {color, {}, mat.metallic_factor, mat.roughness_factor};
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
    double transmission = mat.transmission_factor;
    if (const auto &binding = mat.transmission_texture)
    {
        transmission *= sample(*binding, face, weights, gradients).r;
    }
    return {color, emission, metallic, roughness, occlusion, transmission};
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

FaceCulling GltfScene::face_culling(CullFace single_sided) const &
{
    return [this, single_sided](SurfaceId id)
    {
        const auto &face = instance(id.instance).triangles[std::to_underlying(id.primitive)];
        return material(face).double_sided ? CullFace::None : single_sided;
    };
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
