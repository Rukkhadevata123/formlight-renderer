export module formlight.preview;

import std;
export import formlight.gltf;
export import formlight.scene_config;
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

export void add_preview_ground(GltfScene &scene, const Bounds3 &subject)
{
    const auto center = subject.center();
    const double half = 3 * subject.radius(), y = subject.low.y;
    const TriangleVertex a{{center.x - half, y, center.z - half}}, b{{center.x - half, y, center.z + half}},
        c{{center.x + half, y, center.z + half}}, d{{center.x + half, y, center.z - half}};
    scene.add_solid_mesh({Triangle{.vertices = {a, b, c}}, Triangle{.vertices = {a, c, d}}}, {0.4f, 0.4f, 0.4f, 1});
}

PreviewView clip_to_bounds(const PinholeCamera &camera, const Bounds3 &bounds, double minimum_near)
{
    const double center = camera.to_view(bounds.center()).depth;
    const double radius = bounds.radius();
    const double near_depth = std::max(minimum_near, center - radius);
    return {camera, {near_depth, std::max(center + radius, 2 * minimum_near)}};
}

// Frame the subject, but include all scene geometry in the camera's depth range.
export [[nodiscard]] PreviewView frame_scene(const GltfScene &scene, ImageExtent extent,
                                             std::optional<Bounds3> subject = {},
                                             double fov = CameraSettings{}.vertical_fov_degrees *
                                                          (std::numbers::pi / 180))
{
    const auto bounds = world_bounds(scene.instances());
    const auto framing = subject.value_or(bounds);
    const auto center = framing.center();
    const double radius = framing.radius();
    const double half_angle =
        std::atan(std::tan(fov / 2) * std::min(1.0, static_cast<double>(extent.width) / extent.height));
    const double distance = 1.05 * radius / std::sin(half_angle);
    constexpr CameraSettings defaults;
    const Vec3 direction = unit_vector(defaults.position - defaults.target, "Invalid default camera direction");
    const PinholeCamera camera{{center + direction * distance, center, defaults.up, fov}, extent};
    return clip_to_bounds(camera, bounds, distance * 1e-5);
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
    const auto *directional = lighting ? std::get_if<DirectionalLighting>(&*lighting) : nullptr;
    const auto *area = lighting ? std::get_if<AreaLighting>(&*lighting) : nullptr;
    std::optional<DirectionalShadowMap> shadow_map;
    if (directional && directional->shadows == ShadowMethod::Map)
    {
        shadow_map.emplace(scene.instances(), directional->light);
    }
    const auto in_shadow = [&](const MeshInstance &instance, const Triangle &face, Barycentrics weights)
    {
        if (shadow_map)
        {
            const Vec3 world = instance.transform.to_world_point(face.position_at(weights));
            const Vec3 geometric_normal = instance.transform.to_world_normal(face.geometric_normal());
            return !shadow_map->visible(world, geometric_normal);
        }
        if (directional && directional->shadows == ShadowMethod::Ray)
        {
            const Ray ray = SurfaceRayOrigin{instance, face, weights}.along(directional->light.toward_light());
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
                if (area)
                {
                    const auto pixel = static_cast<std::uint64_t>(y) * extent.width + x;
                    const auto blockers = area->shadows ? scene.instances() : std::span<const MeshInstance>{};
                    const Vec3 normal = instance.transform.to_world_normal(face.normal_at(weights));
                    const double weight = sample_area_diffuse(area->light, SurfaceRayOrigin{instance, face, weights},
                                                              normal, area->sampling, pixel, blockers);
                    color = {static_cast<float>(color.r * weight), static_cast<float>(color.g * weight),
                             static_cast<float>(color.b * weight), color.a};
                }
                else if (directional)
                {
                    const Vec3 normal = instance.transform.to_world_normal(face.normal_at(weights));
                    color = directional->light.diffuse(color, normal);
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

export [[nodiscard]] ColorImage render_scene(const RenderRequest &request)
{
    auto scene = GltfScene::load_glb(request.asset, GltfContent::BaseColor);
    if (!scene)
    {
        throw std::runtime_error(scene.error());
    }
    std::optional<Bounds3> subject;
    if (request.ground)
    {
        subject = world_bounds(scene->instances());
        add_preview_ground(*scene, *subject);
    }
    if (request.camera)
    {
        const auto bounds = world_bounds(scene->instances());
        const auto view = clip_to_bounds(*request.camera, bounds, bounds.radius() * 1e-5);
        return render_preview(*scene, view, request.lighting);
    }
    return render_preview(*scene, frame_scene(*scene, request.extent, subject, request.vertical_fov_radians),
                          request.lighting);
}
} // namespace formlight
