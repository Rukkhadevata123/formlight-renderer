export module formlight.ray_query;

import std;
export import formlight.mesh_instance;

namespace formlight
{
export struct RayInterval
{
    double min, max;
};

export struct RayHit
{
    TrianglePoint surface;
    double t;
};

// Start on the side of the geometric surface that the outgoing world-space direction points into.
export [[nodiscard]] Ray spawn_surface_ray(const MeshInstance &instance, const Triangle &face, Barycentrics weights,
                                           Vec3 direction)
{
    const auto &[a, b, c] = face.vertices;
    const Vec3 local_magnitudes = abs_components(a.position) + abs_components(b.position - a.position) * weights.b +
                                  abs_components(c.position - a.position) * weights.c;
    const Vec3 world = instance.transform.to_world_point(face.position_at(weights));
    Vec3 normal = instance.transform.to_world_normal(face.geometric_normal());
    if (dot(normal, direction) < 0)
    {
        normal = normal * -1;
    }

    // Project a roundoff allowance onto the normal so distant tangential coordinates do not widen thin gaps.
    const double scale = dot(abs_components(normal), instance.transform.world_point_magnitudes(local_magnitudes));
    const Vec3 origin = world + normal * (32 * std::numeric_limits<double>::epsilon() * scale);
    const auto outward = [](double value, double component)
    {
        if (component == 0)
        {
            return value;
        }
        return std::nextafter(value, std::copysign(std::numeric_limits<double>::infinity(), component));
    };
    return {{outward(origin.x, normal.x), outward(origin.y, normal.y), outward(origin.z, normal.z)}, direction};
}

// Two-sided Möller–Trumbore intersection in the open interval (min, max).
[[nodiscard]] std::optional<RayHit> intersect_triangle(const Ray &ray, const Triangle &triangle, InstanceId instance,
                                                       RayInterval interval) noexcept
{
    const auto &[a, b, c] = triangle.vertices;
    const Vec3 ab = b.position - a.position;
    const Vec3 ac = c.position - a.position;
    const Vec3 p = cross(ray.direction, ac);
    const double determinant = dot(ab, p);
    if (determinant == 0.0)
    {
        return std::nullopt;
    }

    const Vec3 from_a = ray.origin - a.position;
    const double weight_b = dot(from_a, p) / determinant;
    if (weight_b < 0.0 || weight_b > 1.0)
    {
        return std::nullopt;
    }
    const Vec3 q = cross(from_a, ab);
    const double weight_c = dot(ray.direction, q) / determinant;
    if (weight_c < 0.0 || weight_b + weight_c > 1.0)
    {
        return std::nullopt;
    }

    const double t = dot(ac, q) / determinant;
    if (!(t > interval.min && t < interval.max))
    {
        return std::nullopt;
    }
    return RayHit{.surface = {{instance, triangle.id}, {1.0 - weight_b - weight_c, weight_b, weight_c}}, .t = t};
}

// Brute-force nearest hit in (min, max). Exact ties use the smaller (instance, primitive) pair.
export [[nodiscard]] std::optional<RayHit> intersect(const Ray &ray, std::span<const MeshInstance> instances,
                                                     RayInterval interval) noexcept
{
    std::optional<RayHit> nearest;
    for (const auto &instance : instances)
    {
        const Ray local = instance.transform.to_local_ray(ray);
        for (const auto &triangle : instance.triangles)
        {
            const auto hit = intersect_triangle(local, triangle, instance.id, interval);
            if (hit && (!nearest || std::tie(hit->t, hit->surface.id) < std::tie(nearest->t, nearest->surface.id)))
            {
                nearest = hit;
            }
        }
    }
    return nearest;
}

// For opaque triangles, any hit in the open interval blocks the ray.
export [[nodiscard]] bool occluded(const Ray &ray, std::span<const MeshInstance> instances,
                                   RayInterval interval) noexcept
{
    for (const auto &instance : instances)
    {
        const Ray local = instance.transform.to_local_ray(ray);
        for (const auto &triangle : instance.triangles)
        {
            if (intersect_triangle(local, triangle, instance.id, interval))
            {
                return true;
            }
        }
    }
    return false;
}
} // namespace formlight
