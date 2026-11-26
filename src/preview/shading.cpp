module formlight.preview;

import std;
import formlight.ray_query;
import formlight.shadow_map;

namespace formlight
{
namespace
{
constexpr LinearColor background{0.035f, 0.045f, 0.06f, 1.0f};

std::uint64_t pixel_index(PixelCoord pixel, ImageExtent extent) noexcept
{
    return static_cast<std::uint64_t>(pixel.y) * extent.width + pixel.x;
}
} // namespace

PreviewRenderer::PreviewRenderer(const GltfScene &scene, std::optional<PreviewLighting> lighting)
    : scene_(scene), lighting_(std::move(lighting))
{
    if (!lighting_)
    {
        return;
    }
    const auto *directional = std::get_if<DirectionalLighting>(&*lighting_);
    const auto *area = std::get_if<AreaLighting>(&*lighting_);
    if (directional && directional->shadows == ShadowMethod::Map)
    {
        shadow_map_.emplace(scene.instances(), directional->light);
    }
    else if ((directional && directional->shadows == ShadowMethod::Ray) || (area && area->shadows))
    {
        blockers_ = TriangleBvh{scene.instances()};
    }
}

LinearColor PreviewRenderer::shade(const RasterHit &hit, std::uint64_t pixel, const std::stop_token &stop,
                                   PixelDiagnostic *record) const
{
    const auto &[id, weights] = hit.surface;
    const auto &instance = scene_.instance(id.instance);
    const auto &face = instance.triangles[std::to_underlying(id.primitive)];
    const auto base = scene_.base_color(face, weights);
    if (record)
    {
        record->surface = DiagnosticSurface{hit, face.material_index, base};
    }
    if (!lighting_)
    {
        return base;
    }

    const Vec3 normal = instance.transform.to_world_normal(face.normal_at(weights));
    if (const auto *area = std::get_if<AreaLighting>(&*lighting_))
    {
        if (record)
        {
            record->light = LightKind::Rectangle;
            record->sampling_seed = area->sampling.seed;
        }
        const double weight =
            sample_area_diffuse(area->light, SurfaceRayOrigin{instance, face, weights}, normal, area->sampling, pixel,
                                blockers_, record ? &record->samples : nullptr, stop);
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
        shadow = trace_shadow(blockers_, ray, {0, std::numeric_limits<double>::infinity()}, identify_blocker);
    }
    else
    {
        shadow.state = ShadowState::Disabled;
    }
    return {std::nullopt, weight, shadow};
}

ColorImage PreviewRenderer::render(const PreviewView &view, std::stop_token stop) const
{
    const ImageExtent extent = view.camera.extent();
    if (extent.width > std::numeric_limits<int>::max() / extent.height)
    {
        throw std::invalid_argument("Preview exceeds visibility buffer capacity");
    }
    ColorImage image{extent, background};
    std::vector<std::optional<RasterHit>> pixels(static_cast<std::size_t>(extent.width) * extent.height);
    const VisibilityBuffer visible{pixels.data(), extent.height, extent.width};
    for (const auto &triangle : RasterTriangle::prepare(view.camera, scene_.instances(), view.depth, stop))
    {
        if (stop.stop_requested())
        {
            throw std::runtime_error("Rendering cancelled");
        }
        triangle.rasterize(visible);
    }
    for (int y = 0; y < extent.height; ++y)
    {
        if (stop.stop_requested())
        {
            throw std::runtime_error("Rendering cancelled");
        }
        for (int x = 0; x < extent.width; ++x)
        {
            if (const auto &hit = visible[y, x])
            {
                image[y, x] = shade(*hit, pixel_index({x, y}, extent), stop);
            }
        }
    }
    return image;
}

PixelDiagnostic PreviewRenderer::diagnose(const PreviewView &view, PixelCoord pixel, std::stop_token stop) const
{
    const ImageExtent extent = view.camera.extent();
    if (pixel.x < 0 || pixel.x >= extent.width || pixel.y < 0 || pixel.y >= extent.height)
    {
        throw std::out_of_range(
            std::format("Pixel ({}, {}) is outside the {} x {} image", pixel.x, pixel.y, extent.width, extent.height));
    }
    const auto center = pixel_center(pixel.x, pixel.y);
    std::optional<RasterHit> nearest;
    for (const auto &triangle : RasterTriangle::prepare(view.camera, scene_.instances(), view.depth, stop))
    {
        if (stop.stop_requested())
        {
            throw std::runtime_error("Pixel inspection cancelled");
        }
        if (const auto hit = triangle.sample(center))
        {
            hit->keep_nearest(nearest);
        }
    }

    PixelDiagnostic report{.pixel = pixel, .color = background};
    if (nearest)
    {
        report.color = shade(*nearest, pixel_index(pixel, extent), stop, &report);
    }
    return report;
}
} // namespace formlight
