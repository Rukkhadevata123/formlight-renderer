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
                         const SurfaceFilter &accept = {}, const FaceCulling &culling = {});
    [[nodiscard]] double visibility(Vec3 world, Vec3 geometric_normal, bool pcf) const noexcept;

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
                                           const SurfaceFilter &accept, const FaceCulling &culling)
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
        const auto local_light = instance.transform.to_local_vector(light.toward_light());
        const auto light_point = [&](const TriangleVertex &v)
        { return project(instance.transform.to_world_point(v.position)); };
        for (const auto &triangle : instance.triangles)
        {
            const SurfaceId id{instance.id, triangle.id};
            if (culling && culled(culling(id), dot(triangle.geometric_normal(), local_light) < 0))
            {
                continue;
            }
            const auto &[a, b, c] = triangle.vertices;
            rasterize(light_point(a), light_point(b), light_point(c), id, accept);
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
    const double area = coverage.twice_area();
    if (area == 0)
    {
        return;
    }
    const auto dx = coverage.edge_dx(), dy = coverage.edge_dy();
    const WeightGradients gradients{{dx[1] / area, dx[2] / area}, {dy[1] / area, dy[2] / area}};
    const auto write_depth = [&](int x, int y, const auto &edges)
    {
        const double depth = (edges[0] * a.z + edges[1] * b.z + edges[2] * c.z) / area + bias;
        auto &nearest = depths_[y * resolution + x];
        // In the orthographic light view the screen weights are already the surface weights.
        if (depth < nearest &&
            (!accept || accept({source, {edges[0] / area, edges[1] / area, edges[2] / area}, gradients})))
        {
            nearest = depth;
        }
    };
    coverage.for_each_sample({resolution, resolution}, write_depth);
}

double DirectionalShadowMap::visibility(Vec3 world, Vec3 geometric_normal, bool pcf) const noexcept
{
    const auto p = project(world);
    if (!(p.x >= 0 && p.x < resolution && p.y >= 0 && p.y < resolution))
    {
        return 1.0;
    }
    const int x = static_cast<int>(p.x), y = static_cast<int>(p.y);
    // The receiver's geometric plane gives the depth change for one texel along each axis.
    double dz_dx = 0, dz_dy = 0;
    const double nz = dot(geometric_normal, forward_);
    if (std::abs(nz) > 32 * std::numeric_limits<double>::epsilon())
    {
        dz_dx = -dot(geometric_normal, right_) / nz * texel_size_;
        dz_dy = -dot(geometric_normal, down_) / nz * texel_size_;
    }
    const auto lit = [&](int column, int row)
    {
        if (column < 0 || column >= resolution || row < 0 || row >= resolution)
        {
            return true;
        }
        const auto center = pixel_center(column, row);
        const double offset = (center.x - p.x) * dz_dx + (center.y - p.y) * dz_dy;
        // At silhouettes the plane extension can lie behind a different surface.
        const double receiver_depth = p.z + std::min(0.0, offset);
        return receiver_depth <= depths_[row * resolution + column] + depth_epsilon_;
    };
    if (!pcf)
    {
        return lit(x, y) ? 1.0 : 0.0;
    }
    int visible = 0;
    for (int row = y - 1; row <= y + 1; ++row)
    {
        for (int column = x - 1; column <= x + 1; ++column)
        {
            visible += lit(column, row);
        }
    }
    return visible / 9.0;
}
} // namespace formlight
