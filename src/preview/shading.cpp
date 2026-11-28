module formlight.preview;

import std;
import formlight.ray_query;
import formlight.shadow_map;

namespace formlight
{
PreviewRenderer::PreviewRenderer(const GltfScene &scene, std::optional<PreviewLighting> lighting, RenderMode mode)
    : scene_(scene), solid_filter_(scene.solid_filter())
{
    configure(mode, std::move(lighting));
}

void PreviewRenderer::configure(RenderMode mode, std::optional<PreviewLighting> lighting)
{
    if (mode == RenderMode::Path)
    {
        if (!path_)
        {
            path_.emplace(scene_);
        }
        lighting_.reset();
        shadow_maps_.clear();
        blockers_.reset();
    }
    else
    {
        if (!scene_.media.empty())
        {
            throw std::invalid_argument("Participating media require path mode");
        }
        if (!scene_.spheres().empty())
        {
            throw std::invalid_argument("Analytic spheres require path mode");
        }
        path_.reset();
    }
    set_lighting(std::move(lighting));
}

void PreviewRenderer::set_lighting(std::optional<PreviewLighting> lighting)
{
    if (path_)
    {
        path_->set_lighting(lighting ? std::span<const ActiveLight>{lighting->direct} : std::span<const ActiveLight>{});
        return;
    }
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
                // Shadow rays travel toward the light, so their front face is the light's back face.
                blockers_ = std::make_shared<TriangleBvh>(scene_.instances(), solid_filter_,
                                                          scene_.face_culling(CullFace::Front));
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
                    maps[i] = std::make_shared<DirectionalShadowMap>(
                        scene_.instances(), directional->light, solid_filter_, scene_.face_culling(CullFace::Back));
                }
            }
        }
    }
    shadow_maps_ = std::move(maps);
    lighting_ = std::move(lighting);
}

LinearColor PreviewRenderer::shade(const RasterHit &hit, PixelCoord pixel, const PreviewView &view, LinearColor behind,
                                   const std::stop_token &stop, SampleDiagnostic *record) const
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
    const Vec3 albedo{base.r, base.g, base.b};
    const double transmitted_share = material.transmission * (1 - material.metallic);
    Vec3 ambient{};
    if (lighting_)
    {
        ambient = component_product(albedo, lighting_->ambient) * (material.occlusion * (1 - transmitted_share));
    }
    if (record)
    {
        // Layers are visited back to front. Keep lighting details for the nearest nonzero layer.
        record->lights.clear();
        record->surface =
            DiagnosticSurface{{hit.surface, hit.view_depth, hit.back_face}, face.material_index, material, ambient};
    }
    // With lighting disabled, the base-color preview supplies the local, non-transmitted light.
    Vec3 radiance = lighting_ ? ambient : albedo * (1 - transmitted_share);
    const bool has_direct = lighting_ && !lighting_->direct.empty();
    if (!has_direct && transmitted_share == 0)
    {
        return material.outgoing_color(radiance);
    }

    const Vec3 normal = scene_.shading_normal(instance, face, weights, gradients, hit.back_face);
    if (record)
    {
        record->surface->shading_normal = normal;
    }
    const Vec3 to_eye = view.camera.ray_for_sample(pixel_center(pixel.x, pixel.y)).direction * -1;
    if (transmitted_share > 0)
    {
        // A smooth thin sheet transmits along the same camera ray after Fresnel reflection.
        const double transmittance =
            transmitted_share * (1 - dielectric_f0) * (1 - schlick_weight(dot(normal, to_eye)));
        const auto through = component_product(albedo, {behind.r, behind.g, behind.b}) * transmittance;
        radiance = radiance + through;
        if (record)
        {
            record->surface->transmitted = through;
        }
    }
    if (!has_direct)
    {
        return material.outgoing_color(radiance);
    }

    const MetallicRoughnessBrdf brdf{material, normal, to_eye, 1 - material.transmission};
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
    return material.outgoing_color(radiance + direct);
}

Vec3 PreviewRenderer::shade_light(std::size_t index, const MetallicRoughnessBrdf &brdf, const MeshInstance &instance,
                                  const Triangle &face, Barycentrics weights, std::uint64_t pixel,
                                  AreaLightPattern pattern, std::stop_token stop, DiagnosticLight *record) const
{
    const auto &light = lighting_->direct[index].source;
    if (const auto *area = std::get_if<AreaLighting>(&light))
    {
        return area->evaluate(SurfaceRayOrigin{instance, face, weights}, brdf, pixel, blockers_.get(), pattern, record,
                              stop);
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
        const double visibility = map->visibility(world, geometric_normal, light.pcf);
        if (visibility == 0 || visibility == 1)
        {
            return {visibility == 0 ? ShadowState::Blocked : ShadowState::Visible};
        }
        return {.state = ShadowState::PartiallyVisible, .visibility = visibility};
    }
    if (light.shadows == ShadowMethod::Ray)
    {
        const Ray ray = SurfaceRayOrigin{instance, face, weights}.along(light.light.toward_light());
        return trace_shadow(*blockers_, ray, {0, std::numeric_limits<double>::infinity()}, identify_blocker);
    }
    return {ShadowState::Disabled};
}

} // namespace formlight
