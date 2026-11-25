export module formlight.base_color_preview;

import std;
export import formlight.gltf;
export import formlight.raster;

namespace formlight
{
export struct PreviewView
{
    PinholeCamera camera;
    ViewDepthRange depth{};
};

// Fit the transformed scene bounds in a perspective view with world +Y up.
export [[nodiscard]] PreviewView frame_scene(const GltfScene &scene, ImageExtent extent)
{
    const double infinity = std::numeric_limits<double>::infinity();
    Vec3 lo{infinity, infinity, infinity}, hi{-infinity, -infinity, -infinity};
    for (const auto &instance : scene.instances())
    {
        for (const auto &triangle : instance.triangles)
        {
            for (const auto &vertex : triangle.vertices)
            {
                const auto p = instance.transform.to_world_point(vertex.position);
                lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
                hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
            }
        }
    }
    if (lo.x > hi.x)
    {
        throw std::invalid_argument("The scene has no triangles to render");
    }
    const Vec3 center{std::midpoint(lo.x, hi.x), std::midpoint(lo.y, hi.y), std::midpoint(lo.z, hi.z)};
    const Vec3 half = hi - center;
    const double radius = std::hypot(half.x, half.y, half.z);
    if (!(radius > 0) || !std::isfinite(radius))
    {
        throw std::invalid_argument("The scene has no finite spatial extent");
    }

    constexpr double fov = std::numbers::pi / 4;
    const double half_angle =
        std::atan(std::tan(fov / 2) * std::min(1.0, static_cast<double>(extent.width) / extent.height));
    const double distance = 1.05 * radius / std::sin(half_angle);
    const Vec3 direction = Vec3{1.1, 0.8, 1.5} / std::hypot(1.1, 0.8, 1.5);
    return {PinholeCamera{{center + direction * distance, center, {0, 1, 0}, fov}, extent},
            {distance - radius, distance + radius}};
}

// Unlit, opaque base-color pass. The resulting pixels are linear and can be consumed independently of PNG output.
export [[nodiscard]] ColorImage render_base_color(const GltfScene &scene, const PreviewView &view)
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
    for (int y = 0; y < extent.height; ++y)
    {
        for (int x = 0; x < extent.width; ++x)
        {
            if (const auto &hit = visible[y, x])
            {
                image[y, x] = scene.base_color(hit->surface);
            }
        }
    }
    return image;
}
} // namespace formlight
