export module formlight.shadow_map;

import std;
import formlight.mesh_instance;
import formlight.raster_coverage;
import formlight.lighting;
import formlight.camera;

namespace formlight
{
// Orthographic light-space coordinates, with X/Y in texels and Z increasing away from the light.
export class DirectionalShadowMap
{
  public:
    DirectionalShadowMap(std::span<const MeshInstance> instances, const DirectionalLight &light,
                         const SurfaceFilter &accept = {});
    [[nodiscard]] bool visible(Vec3 world, Vec3 geometric_normal) const noexcept;

  private:
    static constexpr int resolution = 1024;
    Vec3 origin_{}, right_{}, down_{}, forward_{};
    double texel_size_{}, depth_epsilon_{};
    std::vector<double> depths_;

    [[nodiscard]] Vec3 project(Vec3 world) const noexcept
    {
        const Vec3 offset = world - origin_;
        return {resolution * 0.5 + dot(offset, right_) / texel_size_,
                resolution * 0.5 + dot(offset, down_) / texel_size_, dot(offset, forward_)};
    }
    void rasterize(Vec3 a, Vec3 b, Vec3 c, SurfaceId source, const SurfaceFilter &accept);
};

DirectionalShadowMap::DirectionalShadowMap(std::span<const MeshInstance> instances, const DirectionalLight &light,
                                           const SurfaceFilter &accept)
    : depths_(resolution * resolution, std::numeric_limits<double>::infinity())
{
    const auto bounds = world_bounds(instances);
    origin_ = bounds.center();
    texel_size_ = 2 * bounds.radius() / (resolution - 2);
    forward_ = light.toward_light() * -1;
    const Vec3 up = std::abs(forward_.y) < 0.9 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
    right_ = unit_vector(cross(forward_, up), "Invalid light view");
    down_ = cross(forward_, right_);

    const double scale = std::max({bounds.radius(), std::abs(origin_.x), std::abs(origin_.y), std::abs(origin_.z)});
    depth_epsilon_ = 32 * std::numeric_limits<double>::epsilon() * scale;
    for (const auto &instance : instances)
    {
        const auto light_point = [&](const TriangleVertex &v)
        { return project(instance.transform.to_world_point(v.position)); };
        for (const auto &triangle : instance.triangles)
        {
            const auto &[a, b, c] = triangle.vertices;
            rasterize(light_point(a), light_point(b), light_point(c), {instance.id, triangle.id}, accept);
        }
    }
}

void DirectionalShadowMap::rasterize(Vec3 a, Vec3 b, Vec3 c, SurfaceId source, const SurfaceFilter &accept)
{
    const ScreenTriangle coverage{{a.x, a.y}, {b.x, b.y}, {c.x, c.y}};
    const Vec3 plane = cross(b - a, c - a);
    if (plane.z == 0)
    {
        return;
    }
    // Allow for the caster depth varying by up to half a texel along each axis.
    const double bias = 0.5 * (std::abs(plane.x) + std::abs(plane.y)) / std::abs(plane.z);
    const auto write_depth = [&](int x, int y, const auto &edges)
    {
        const double area = coverage.twice_area();
        const double depth = (edges[0] * a.z + edges[1] * b.z + edges[2] * c.z) / area + bias;
        auto &nearest = depths_[y * resolution + x];
        // In the orthographic light view the screen weights are already the surface weights.
        if (depth < nearest && (!accept || accept({source, {edges[0] / area, edges[1] / area, edges[2] / area}})))
        {
            nearest = depth;
        }
    };
    coverage.for_each_sample({resolution, resolution}, write_depth);
}

bool DirectionalShadowMap::visible(Vec3 world, Vec3 geometric_normal) const noexcept
{
    const auto p = project(world);
    if (!(p.x >= 0 && p.x < resolution && p.y >= 0 && p.y < resolution))
    {
        return true;
    }
    const int x = static_cast<int>(p.x), y = static_cast<int>(p.y);
    double receiver_depth = p.z;
    const double nz = dot(geometric_normal, forward_);
    if (std::abs(nz) > 32 * std::numeric_limits<double>::epsilon())
    {
        // Compare both depths at the texel center, using the geometric plane even for smooth shading.
        const auto center = pixel_center(x, y);
        const double dx = (center.x - p.x) * texel_size_, dy = (center.y - p.y) * texel_size_;
        // At a silhouette the plane extension may lie behind a different surface.
        // Only shift the receiver toward the light when it crosses a texel.
        const double depth_offset = -(dx * dot(geometric_normal, right_) + dy * dot(geometric_normal, down_)) / nz;
        receiver_depth += std::min(0.0, depth_offset);
    }
    return receiver_depth <= depths_[y * resolution + x] + depth_epsilon_;
}
} // namespace formlight
