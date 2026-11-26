module formlight.preview;

import std;
import formlight.ray_query;
import formlight.shadow_map;

namespace formlight
{
namespace
{
constexpr LinearColor background{0.035f, 0.045f, 0.06f, 1.0f};

class PreviewShader
{
  public:
    PreviewShader(const GltfScene &scene, const std::optional<PreviewLighting> &lighting)
        : scene_(scene), directional_(lighting ? std::get_if<DirectionalLighting>(&*lighting) : nullptr),
          area_(lighting ? std::get_if<AreaLighting>(&*lighting) : nullptr)
    {
        if (directional_ && directional_->shadows == ShadowMethod::Map)
        {
            shadow_map_.emplace(scene.instances(), directional_->light);
        }
        else if ((directional_ && directional_->shadows == ShadowMethod::Ray) || (area_ && area_->shadows))
        {
            blockers_ = TriangleBvh{scene.instances()};
        }
    }

    [[nodiscard]] LinearColor shade(const RasterHit &hit, std::uint64_t pixel, PixelDiagnostic *record = nullptr) const
    {
        const auto &[id, weights] = hit.surface;
        const auto &instance = scene_.instance(id.instance);
        const auto &face = instance.triangles[std::to_underlying(id.primitive)];
        const auto base = scene_.base_color(face, weights);
        if (record)
        {
            record->surface = DiagnosticSurface{hit, face.material_index, base};
        }
        if (!area_ && !directional_)
        {
            return base;
        }

        const Vec3 normal = instance.transform.to_world_normal(face.normal_at(weights));
        if (area_)
        {
            if (record)
            {
                record->light = LightKind::Rectangle;
                record->sampling_seed = area_->sampling.seed;
            }
            const double weight =
                sample_area_diffuse(area_->light, SurfaceRayOrigin{instance, face, weights}, normal, area_->sampling,
                                    pixel, blockers_, record ? &record->samples : nullptr);
            return scale_rgb(base, weight);
        }

        const LightSample sample = directional_sample(instance, face, weights, normal, record != nullptr);
        if (record)
        {
            record->light = LightKind::Directional;
            record->samples.push_back(sample);
        }
        return scale_rgb(base, sample.visible_weight());
    }

  private:
    [[nodiscard]] LightSample directional_sample(const MeshInstance &instance, const Triangle &face,
                                                 Barycentrics weights, Vec3 normal, bool identify_blocker) const
    {
        const double weight = directional_->light.diffuse_weight(normal);
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
        else if (directional_->shadows == ShadowMethod::Ray)
        {
            const Ray ray = SurfaceRayOrigin{instance, face, weights}.along(directional_->light.toward_light());
            shadow = trace_shadow(blockers_, ray, {0, std::numeric_limits<double>::infinity()}, identify_blocker);
        }
        else
        {
            shadow.state = ShadowState::Disabled;
        }
        return {std::nullopt, weight, shadow};
    }

    const GltfScene &scene_;
    const DirectionalLighting *directional_;
    const AreaLighting *area_;
    std::optional<DirectionalShadowMap> shadow_map_;
    TriangleBvh blockers_;
};

std::uint64_t pixel_index(PixelCoord pixel, ImageExtent extent) noexcept
{
    return static_cast<std::uint64_t>(pixel.y) * extent.width + pixel.x;
}
} // namespace

ColorImage render_preview(const GltfScene &scene, const PreviewView &view, std::optional<PreviewLighting> lighting)
{
    const ImageExtent extent = view.camera.extent();
    if (extent.width > std::numeric_limits<int>::max() / extent.height)
    {
        throw std::invalid_argument("Preview exceeds visibility buffer capacity");
    }
    ColorImage image{extent, background};
    std::vector<std::optional<RasterHit>> pixels(static_cast<std::size_t>(extent.width) * extent.height);
    const VisibilityBuffer visible{pixels.data(), extent.height, extent.width};
    for (const auto &triangle : RasterTriangle::prepare(view.camera, scene.instances(), view.depth))
    {
        triangle.rasterize(visible);
    }
    const PreviewShader shader{scene, lighting};
    for (int y = 0; y < extent.height; ++y)
    {
        for (int x = 0; x < extent.width; ++x)
        {
            if (const auto &hit = visible[y, x])
            {
                image[y, x] = shader.shade(*hit, pixel_index({x, y}, extent));
            }
        }
    }
    return image;
}

PixelDiagnostic diagnose_pixel(const GltfScene &scene, const PreviewView &view, PixelCoord pixel,
                               std::optional<PreviewLighting> lighting)
{
    const ImageExtent extent = view.camera.extent();
    if (pixel.x < 0 || pixel.x >= extent.width || pixel.y < 0 || pixel.y >= extent.height)
    {
        throw std::out_of_range(
            std::format("Pixel ({}, {}) is outside the {} x {} image", pixel.x, pixel.y, extent.width, extent.height));
    }
    const auto center = pixel_center(pixel.x, pixel.y);
    std::optional<RasterHit> nearest;
    for (const auto &triangle : RasterTriangle::prepare(view.camera, scene.instances(), view.depth))
    {
        if (const auto hit = triangle.sample(center))
        {
            hit->keep_nearest(nearest);
        }
    }

    PixelDiagnostic report{.pixel = pixel, .color = background};
    if (nearest)
    {
        const PreviewShader shader{scene, lighting};
        report.color = shader.shade(*nearest, pixel_index(pixel, extent), &report);
    }
    return report;
}
} // namespace formlight
