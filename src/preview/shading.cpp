module formlight.preview;

import std;
import formlight.ray_query;
import formlight.shadow_map;

namespace formlight
{
namespace
{
LinearColor outgoing_color(const SurfaceMaterial &surface, Vec3 reflected) noexcept
{
    const auto &emission = surface.emission;
    return {static_cast<float>(reflected.x + emission.r), static_cast<float>(reflected.y + emission.g),
            static_cast<float>(reflected.z + emission.b), surface.base_color.a};
}
} // namespace

PreviewRenderer::PreviewRenderer(const GltfScene &scene, std::optional<PreviewLighting> lighting)
    : scene_(scene), solid_filter_(scene.solid_filter())
{
    set_lighting(std::move(lighting));
}

void PreviewRenderer::set_lighting(std::optional<PreviewLighting> lighting)
{
    std::vector<std::shared_ptr<const DirectionalShadowMap>> maps;
    if (lighting)
    {
        maps.resize(lighting->direct.size());
        for (std::size_t i = 0; i < maps.size(); ++i)
        {
            const auto &direct = lighting->direct[i].source;
            const auto *directional = std::get_if<DirectionalLighting>(&direct);
            const auto *area = std::get_if<AreaLighting>(&direct);
            const auto *point = std::get_if<PointLighting>(&direct);
            if (!blockers_ && ((directional && directional->shadows == ShadowMethod::Ray) || (area && area->shadows) ||
                               (point && point->shadows)))
            {
                blockers_ = std::make_shared<TriangleBvh>(scene_.instances(), solid_filter_);
            }
            if (directional && directional->shadows == ShadowMethod::Map)
            {
                if (i < shadow_maps_.size() && shadow_maps_[i] &&
                    std::get<DirectionalLighting>(lighting_->direct[i].source).light.toward_light() ==
                        directional->light.toward_light())
                {
                    maps[i] = shadow_maps_[i];
                }
                else
                {
                    maps[i] =
                        std::make_shared<DirectionalShadowMap>(scene_.instances(), directional->light, solid_filter_);
                }
            }
        }
    }
    shadow_maps_ = std::move(maps);
    lighting_ = std::move(lighting);
}

LinearColor PreviewRenderer::shade(const RasterHit &hit, PixelCoord pixel, const PreviewView &view,
                                   const std::stop_token &stop, bool back_face, PixelDiagnostic *record) const
{
    const auto &[id, weights, gradients] = hit.surface;
    const auto &instance = scene_.instance(id.instance);
    const auto &face = instance.triangles[std::to_underlying(id.primitive)];
    const auto material = scene_.surface_material(face, weights, gradients);
    const auto &base = material.base_color;
    if (base.a == 0)
    {
        return base;
    }
    Vec3 ambient{};
    if (lighting_)
    {
        const auto rgb = lighting_->ambient;
        ambient = Vec3{base.r * rgb.x, base.g * rgb.y, base.b * rgb.z} * material.occlusion;
    }
    if (record)
    {
        // Layers are visited back to front. Keep lighting details for the nearest nonzero layer.
        record->lights.clear();
        record->surface = DiagnosticSurface{hit, face.material_index, material, ambient};
    }
    if (!lighting_)
    {
        // The no-light preview shows the base color at unit weight.
        return outgoing_color(material, {base.r, base.g, base.b});
    }
    if (lighting_->direct.empty())
    {
        return outgoing_color(material, ambient);
    }

    const Vec3 normal = scene_.shading_normal(instance, face, weights, gradients, back_face);
    if (record)
    {
        record->surface->shading_normal = normal;
    }
    const Vec3 to_eye = view.camera.ray_for_sample(pixel_center(pixel.x, pixel.y)).direction * -1;
    const MetallicRoughnessBrdf brdf{material, normal, to_eye};
    const auto pixel_index = static_cast<std::uint64_t>(pixel.y) * view.camera.extent().width + pixel.x;
    Vec3 direct{};
    for (std::size_t i = 0; i < lighting_->direct.size(); ++i)
    {
        if (stop.stop_requested())
        {
            throw std::runtime_error("Rendering cancelled");
        }
        auto *light_record = record ? &record->lights.emplace_back() : nullptr;
        const Vec3 contribution =
            shade_light(i, brdf, instance, face, weights, pixel_index, view.area_light_pattern, stop, light_record);
        direct = direct + contribution;
        if (light_record)
        {
            light_record->scene_index = lighting_->direct[i].scene_index;
            light_record->direct = contribution;
        }
    }
    return outgoing_color(material, ambient + direct);
}

Vec3 PreviewRenderer::shade_light(std::size_t index, const MetallicRoughnessBrdf &brdf, const MeshInstance &instance,
                                  const Triangle &face, Barycentrics weights, std::uint64_t pixel,
                                  AreaLightPattern pattern, std::stop_token stop, DiagnosticLight *record) const
{
    const auto &light = lighting_->direct[index].source;
    if (const auto *area = std::get_if<AreaLighting>(&light))
    {
        if (record)
        {
            record->kind = LightKind::Rectangle;
            record->sampling_seed = area->sampling.seed;
        }
        return sample_area_light(area->light, SurfaceRayOrigin{instance, face, weights}, brdf, area->sampling, pixel,
                                 area->shadows ? blockers_.get() : nullptr, pattern,
                                 record ? &record->samples : nullptr, stop);
    }

    LightSample sample;
    LightKind kind;
    if (const auto *point = std::get_if<PointLighting>(&light))
    {
        kind = LightKind::Point;
        const SurfaceRayOrigin receiver{instance, face, weights};
        sample = evaluate_finite_light_sample(brdf, point->light.incident(receiver.position()), receiver,
                                              point->light.position(), point->shadows ? blockers_.get() : nullptr,
                                              record != nullptr);
    }
    else
    {
        kind = LightKind::Directional;
        const auto &directional = std::get<DirectionalLighting>(light);
        sample = evaluate_light_sample(brdf, directional.light.incident(), std::nullopt,
                                       [&]
                                       {
                                           return directional_shadow(directional, shadow_maps_[index].get(), instance,
                                                                     face, weights, record != nullptr);
                                       });
    }
    if (record)
    {
        record->kind = kind;
        record->samples.push_back(sample);
    }
    return sample.direct;
}

ShadowResult PreviewRenderer::directional_shadow(const DirectionalLighting &light, const DirectionalShadowMap *map,
                                                 const MeshInstance &instance, const Triangle &face,
                                                 Barycentrics weights, bool identify_blocker) const
{
    if (map)
    {
        const Vec3 world = instance.transform.to_world_point(face.position_at(weights));
        const Vec3 geometric_normal = instance.transform.to_world_normal(face.geometric_normal());
        return {map->visible(world, geometric_normal) ? ShadowState::Visible : ShadowState::Blocked};
    }
    if (light.shadows == ShadowMethod::Ray)
    {
        const Ray ray = SurfaceRayOrigin{instance, face, weights}.along(light.light.toward_light());
        return trace_shadow(*blockers_, ray, {0, std::numeric_limits<double>::infinity()}, identify_blocker);
    }
    return {ShadowState::Disabled};
}

} // namespace formlight
