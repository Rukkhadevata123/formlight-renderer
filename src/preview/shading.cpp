module formlight.preview;

import std;
import formlight.ray_query;
import formlight.shadow_map;

namespace formlight
{

PreviewRenderer::PreviewRenderer(const GltfScene &scene, std::optional<PreviewLighting> lighting)
    : scene_(scene), solid_filter_(scene.solid_filter())
{
    set_lighting(std::move(lighting));
}

void PreviewRenderer::set_lighting(std::optional<PreviewLighting> lighting)
{
    const auto *directional = lighting ? std::get_if<DirectionalLighting>(&*lighting) : nullptr;
    const auto *area = lighting ? std::get_if<AreaLighting>(&*lighting) : nullptr;
    if (!blockers_ && ((directional && directional->shadows == ShadowMethod::Ray) || (area && area->shadows)))
    {
        blockers_ = std::make_shared<TriangleBvh>(scene_.instances(), solid_filter_);
    }
    if (directional && directional->shadows == ShadowMethod::Map)
    {
        const auto direction = directional->light.toward_light();
        if (!shadow_map_ || std::get<DirectionalLighting>(*lighting_).light.toward_light() != direction)
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

LinearColor PreviewRenderer::shade(const RasterHit &hit, std::uint64_t pixel, AreaLightPattern area_light_pattern,
                                   const std::stop_token &stop, bool back_face, PixelDiagnostic *record) const
{
    const auto &[id, weights] = hit.surface;
    const auto &instance = scene_.instance(id.instance);
    const auto &face = instance.triangles[std::to_underlying(id.primitive)];
    const auto base = scene_.base_color(face, weights);
    if (base.a == 0)
    {
        return base;
    }
    if (record)
    {
        // Layers are visited back to front. Keep lighting details for the nearest nonzero layer.
        record->samples.clear();
        record->surface = DiagnosticSurface{hit, face.material_index, base};
    }
    if (!lighting_)
    {
        return base;
    }

    const Vec3 local_normal = face.normal_at(weights);
    const Vec3 normal = instance.transform.to_world_normal(back_face ? local_normal * -1 : local_normal);
    if (const auto *area = std::get_if<AreaLighting>(&*lighting_))
    {
        if (record)
        {
            record->light = LightKind::Rectangle;
            record->sampling_seed = area->sampling.seed;
        }
        const double weight = sample_area_diffuse(area->light, SurfaceRayOrigin{instance, face, weights}, normal,
                                                  area->sampling, pixel, area->shadows ? blockers_.get() : nullptr,
                                                  area_light_pattern, record ? &record->samples : nullptr, stop);
        return scale_rgb(base, weight);
    }

    const LightSample sample = directional_sample(std::get<DirectionalLighting>(*lighting_), instance, face, weights,
                                                  normal, record != nullptr);
    if (record)
    {
        record->light = LightKind::Directional;
        record->samples.push_back(sample);
    }
    return scale_rgb(base, sample.visible_weight());
}

LightSample PreviewRenderer::directional_sample(const DirectionalLighting &light, const MeshInstance &instance,
                                                const Triangle &face, Barycentrics weights, Vec3 normal,
                                                bool identify_blocker) const
{
    const double weight = light.light.diffuse_weight(normal);
    ShadowResult shadow;
    if (weight <= 0)
    {
        return {std::nullopt, weight, shadow};
    }
    if (shadow_map_)
    {
        const Vec3 world = instance.transform.to_world_point(face.position_at(weights));
        const Vec3 geometric_normal = instance.transform.to_world_normal(face.geometric_normal());
        shadow.state = shadow_map_->visible(world, geometric_normal) ? ShadowState::Visible : ShadowState::Blocked;
    }
    else if (light.shadows == ShadowMethod::Ray)
    {
        const Ray ray = SurfaceRayOrigin{instance, face, weights}.along(light.light.toward_light());
        shadow = trace_shadow(*blockers_, ray, {0, std::numeric_limits<double>::infinity()}, identify_blocker);
    }
    else
    {
        shadow.state = ShadowState::Disabled;
    }
    return {std::nullopt, weight, shadow};
}

} // namespace formlight
