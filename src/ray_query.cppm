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

// Optional counters owned by the caller, so the tree stays read-only between queries.
export struct RayQueryStats
{
    std::uint64_t rays{}, triangle_tests{};
};

// Borrows the instances and their triangle storage. Rebuild after either geometry or transforms change.
export class TriangleBvh
{
  public:
    TriangleBvh() = default;
    explicit TriangleBvh(std::span<const MeshInstance> instances);
    [[nodiscard]] bool empty() const noexcept
    {
        return nodes_.empty();
    }
    [[nodiscard]] std::optional<RayHit> intersect(const Ray &ray, RayInterval interval,
                                                  RayQueryStats *stats = nullptr) const noexcept;
    [[nodiscard]] bool occluded(const Ray &ray, RayInterval interval, RayQueryStats *stats = nullptr) const noexcept;

  private:
    struct Primitive
    {
        std::size_t instance, triangle;
    };
    struct BuildPrimitive;

    struct Node
    {
        Bounds3 bounds;
        // A leaf indexes primitives. An interior node's left child follows it immediately.
        std::size_t first_primitive_or_right_child, count;
    };
    std::span<const MeshInstance> instances_;
    std::vector<Node> nodes_;
    std::vector<Primitive> primitives_;

    std::size_t build(std::span<BuildPrimitive> primitives);
    enum class Query
    {
        Closest,
        Any
    };
    template <Query Kind>
    [[nodiscard]] std::optional<RayHit> trace(const Ray &ray, RayInterval interval,
                                              RayQueryStats *stats) const noexcept;
};

// Cache both sides of a hit when several light samples share the same receiver.
export class SurfaceRayOrigin
{
  public:
    SurfaceRayOrigin(const MeshInstance &instance, const Triangle &face, Barycentrics weights);
    [[nodiscard]] Vec3 position() const noexcept
    {
        return position_;
    }

    [[nodiscard]] Ray along(Vec3 direction) const noexcept
    {
        return {dot(normal_, direction) < 0 ? negative_ : positive_, direction};
    }

    // The sampled endpoint is at t = 1 even after offsetting the origin.
    [[nodiscard]] Ray to(Vec3 endpoint) const noexcept
    {
        const Vec3 origin = along(endpoint - position_).origin;
        return {origin, endpoint - origin};
    }

  private:
    Vec3 position_, normal_, positive_{}, negative_{};
};

SurfaceRayOrigin::SurfaceRayOrigin(const MeshInstance &instance, const Triangle &face, Barycentrics weights)
    : position_(instance.transform.to_world_point(face.position_at(weights))),
      normal_(instance.transform.to_world_normal(face.geometric_normal()))
{
    const auto &[a, b, c] = face.vertices;
    const Vec3 local_magnitudes = abs_components(a.position) + abs_components(b.position - a.position) * weights.b +
                                  abs_components(c.position - a.position) * weights.c;
    // Project a roundoff allowance onto the normal so distant tangential coordinates do not widen thin gaps.
    const double scale = dot(abs_components(normal_), instance.transform.world_point_magnitudes(local_magnitudes));
    const Vec3 offset = normal_ * (32 * std::numeric_limits<double>::epsilon() * scale);
    const auto outward = [](double value, double component)
    {
        if (component == 0)
        {
            return value;
        }
        return std::nextafter(value, std::copysign(std::numeric_limits<double>::infinity(), component));
    };
    const Vec3 p = position_ + offset, n = position_ - offset;
    positive_ = {outward(p.x, normal_.x), outward(p.y, normal_.y), outward(p.z, normal_.z)};
    negative_ = {outward(n.x, -normal_.x), outward(n.y, -normal_.y), outward(n.z, -normal_.z)};
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

// Traversal order does not determine which surface owns an exact tie.
void keep_nearest(std::optional<RayHit> &nearest, const RayHit &hit) noexcept
{
    if (!nearest || std::tie(hit.t, hit.surface.id) < std::tie(nearest->t, nearest->surface.id))
    {
        nearest = hit;
    }
}

// Brute-force nearest hit in (min, max).
export [[nodiscard]] std::optional<RayHit> intersect(const Ray &ray, std::span<const MeshInstance> instances,
                                                     RayInterval interval) noexcept
{
    std::optional<RayHit> nearest;
    for (const auto &instance : instances)
    {
        const Ray local = instance.transform.to_local_ray(ray);
        for (const auto &triangle : instance.triangles)
        {
            if (const auto hit = intersect_triangle(local, triangle, instance.id, interval))
            {
                keep_nearest(nearest, *hit);
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
