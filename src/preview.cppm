export module formlight.preview;

import std;
export import formlight.gltf;
export import formlight.lighting;
export import formlight.raster;
import formlight.ray_query;
import formlight.shadow_map;

namespace formlight
{
export struct PreviewView
{
    PinholeCamera camera;
    ViewDepthRange depth{};
};

export enum class ShadowMethod
{
    None,
    Map,
    Ray
};

export struct PreviewLighting
{
    DirectionalLight light{{1, 4, 3}};
    ShadowMethod shadows{ShadowMethod::None};
};

export void add_preview_ground(GltfScene &scene, const Bounds3 &subject)
{
    const auto center = subject.center();
    const double half = 3 * subject.radius(), y = subject.low.y;
    const TriangleVertex a{{center.x - half, y, center.z - half}}, b{{center.x - half, y, center.z + half}},
        c{{center.x + half, y, center.z + half}}, d{{center.x + half, y, center.z - half}};
    scene.add_solid_mesh({Triangle{.vertices = {a, b, c}}, Triangle{.vertices = {a, c, d}}}, {0.4f, 0.4f, 0.4f, 1});
}

// Frame the subject, but include all scene geometry in the camera's depth range.
export [[nodiscard]] PreviewView frame_scene(const GltfScene &scene, ImageExtent extent,
                                             std::optional<Bounds3> subject = {})
{
    const auto bounds = world_bounds(scene.instances());
    const auto framing = subject.value_or(bounds);
    const auto center = framing.center();
    const double radius = framing.radius();
    constexpr double fov = std::numbers::pi / 4;
    const double half_angle =
        std::atan(std::tan(fov / 2) * std::min(1.0, static_cast<double>(extent.width) / extent.height));
    const double distance = 1.05 * radius / std::sin(half_angle);
    const Vec3 direction = Vec3{1.1, 0.8, 1.5} / std::hypot(1.1, 0.8, 1.5);
    const PinholeCamera camera{{center + direction * distance, center, {0, 1, 0}, fov}, extent};
    const double scene_depth = camera.to_view(bounds.center()).depth;
    const double scene_radius = bounds.radius();
    return {camera, {std::max(distance * 1e-5, scene_depth - scene_radius), scene_depth + scene_radius}};
}

export [[nodiscard]] ColorImage render_preview(const GltfScene &scene, const PreviewView &view,
                                               std::optional<PreviewLighting> lighting = std::nullopt)
{
    const ImageExtent extent = view.camera.extent();
    if (extent.width > std::numeric_limits<int>::max() / extent.height)
    {
        throw std::invalid_argument("Preview exceeds visibility buffer capacity");
    }
    ColorImage image{extent, {0.035f, 0.045f, 0.06f, 1.0f}};
    std::vector<std::optional<RasterHit>> pixels(static_cast<std::size_t>(extent.width) * extent.height);
    const VisibilityBuffer visible{pixels.data(), extent.height, extent.width};
    for (const auto &triangle : RasterTriangle::prepare(view.camera, scene.instances(), view.depth))
    {
        triangle.rasterize(visible);
    }
    std::optional<DirectionalShadowMap> shadow_map;
    if (lighting && lighting->shadows == ShadowMethod::Map)
    {
        shadow_map.emplace(scene.instances(), lighting->light);
    }
    const auto in_shadow = [&](const MeshInstance &instance, const Triangle &face, Barycentrics weights)
    {
        if (shadow_map)
        {
            const Vec3 world = instance.transform.to_world_point(face.position_at(weights));
            const Vec3 geometric_normal = instance.transform.to_world_normal(face.geometric_normal());
            return !shadow_map->visible(world, geometric_normal);
        }
        if (lighting && lighting->shadows == ShadowMethod::Ray)
        {
            const Ray ray = spawn_surface_ray(instance, face, weights, lighting->light.toward_light());
            return occluded(ray, scene.instances(), {0, std::numeric_limits<double>::infinity()});
        }
        return false;
    };

    for (int y = 0; y < extent.height; ++y)
    {
        for (int x = 0; x < extent.width; ++x)
        {
            if (const auto &hit = visible[y, x])
            {
                const auto &[id, weights] = hit->surface;
                const auto &instance = scene.instance(id.instance);
                const auto &face = instance.triangles[std::to_underlying(id.primitive)];
                auto color = scene.base_color(face, weights);
                if (lighting)
                {
                    const Vec3 normal = instance.transform.to_world_normal(face.normal_at(weights));
                    color = lighting->light.diffuse(color, normal);
                    if ((color.r > 0 || color.g > 0 || color.b > 0) && in_shadow(instance, face, weights))
                    {
                        color = {0, 0, 0, color.a};
                    }
                }
                image[y, x] = color;
            }
        }
    }
    return image;
}
} // namespace formlight
