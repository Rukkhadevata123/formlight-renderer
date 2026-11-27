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
    const auto *direct = lighting ? &lighting->direct : nullptr;
    const auto *directional = std::get_if<DirectionalLighting>(direct);
    const auto *area = std::get_if<AreaLighting>(direct);
    const auto *point = std::get_if<PointLighting>(direct);
    if (!blockers_ && ((directional && directional->shadows == ShadowMethod::Ray) || (area && area->shadows) ||
                       (point && point->shadows)))
    {
        blockers_ = std::make_shared<TriangleBvh>(scene_.instances(), solid_filter_);
    }
    if (directional && directional->shadows == ShadowMethod::Map)
    {
        const auto direction = directional->light.toward_light();
        if (!shadow_map_ || std::get<DirectionalLighting>(lighting_->direct).light.toward_light() != direction)
        {
            shadow_map_ = std::make_shared<DirectionalShadowMap>(scene_.instances(), directional->light, solid_filter_);
        }
    }
    else
    {
        shadow_map_.reset();
    }
    lighting_ = std::move(lighting);
}

LinearColor PreviewRenderer::shade(const RasterHit &hit, PixelCoord pixel, const PreviewView &view,
                                   const std::stop_token &stop, bool back_face, PixelDiagnostic *record) const
{
    const auto &[id, weights] = hit.surface;
    const auto &instance = scene_.instance(id.instance);
    const auto &face = instance.triangles[std::to_underlying(id.primitive)];
    const auto material = scene_.surface_material(face, weights);
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
        record->samples.clear();
        record->surface = DiagnosticSurface{hit, face.material_index, material, ambient};
    }
    if (!lighting_)
    {
        // The no-light preview shows the base color at unit weight.
        return outgoing_color(material, {base.r, base.g, base.b});
    }
    if (std::holds_alternative<std::monostate>(lighting_->direct))
    {
        if (record)
        {
            record->light = LightKind::Ambient;
        }
        return outgoing_color(material, ambient);
    }

    const Vec3 local_normal = face.normal_at(weights);
    const Vec3 normal = instance.transform.to_world_normal(back_face ? local_normal * -1 : local_normal);
    const Vec3 to_eye = view.camera.ray_for_sample(pixel_center(pixel.x, pixel.y)).direction * -1;
    const MetallicRoughnessBrdf brdf{material, normal, to_eye};
    if (const auto *area = std::get_if<AreaLighting>(&lighting_->direct))
    {
        if (record)
        {
            record->light = LightKind::Rectangle;
            record->sampling_seed = area->sampling.seed;
        }
        const auto index = static_cast<std::uint64_t>(pixel.y) * view.camera.extent().width + pixel.x;
        const auto direct = sample_area_light(area->light, SurfaceRayOrigin{instance, face, weights}, brdf,
                                              area->sampling, index, area->shadows ? blockers_.get() : nullptr,
                                              view.area_light_pattern, record ? &record->samples : nullptr, stop);
        return outgoing_color(material, ambient + direct);
    }

    LightSample sample;
    LightKind kind;
    if (const auto *point = std::get_if<PointLighting>(&lighting_->direct))
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
        const auto &directional = std::get<DirectionalLighting>(lighting_->direct);
        sample = evaluate_light_sample(
            brdf, directional.light.incident(), std::nullopt,
            [&] { return directional_shadow(directional, instance, face, weights, record != nullptr); });
    }
    if (record)
    {
        record->light = kind;
        record->samples.push_back(sample);
    }
    return outgoing_color(material, ambient + sample.direct);
}

ShadowResult PreviewRenderer::directional_shadow(const DirectionalLighting &light, const MeshInstance &instance,
                                                 const Triangle &face, Barycentrics weights,
                                                 bool identify_blocker) const
{
    if (shadow_map_)
    {
        const Vec3 world = instance.transform.to_world_point(face.position_at(weights));
        const Vec3 geometric_normal = instance.transform.to_world_normal(face.geometric_normal());
        return {shadow_map_->visible(world, geometric_normal) ? ShadowState::Visible : ShadowState::Blocked};
    }
    if (light.shadows == ShadowMethod::Ray)
    {
        const Ray ray = SurfaceRayOrigin{instance, face, weights}.along(light.light.toward_light());
        return trace_shadow(*blockers_, ray, {0, std::numeric_limits<double>::infinity()}, identify_blocker);
    }
    return {ShadowState::Disabled};
}

} // namespace formlight
