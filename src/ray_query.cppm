export module formlight.ray_query;

import std;
export import formlight.mesh_instance;
export import formlight.sphere;

namespace formlight
{
export struct RayHit
{
    TrianglePoint surface;
    double t;
};

export struct TriangleCoordinates
{
    Barycentrics weights;
    WeightGradients gradients;
};

export struct SphereCoordinates
{
    std::size_t index;
    Vec3 outward_normal;
};

export struct SurfacePoint
{
    SurfaceId id;
    std::variant<TriangleCoordinates, SphereCoordinates> coordinates;

    SurfacePoint(TrianglePoint point) : id(point.id), coordinates(TriangleCoordinates{point.weights, point.gradients})
    {
    }
    SurfacePoint(SurfaceId surface_id, SphereCoordinates sphere) : id(surface_id), coordinates(sphere)
    {
    }

    [[nodiscard]] TrianglePoint triangle() const
    {
        const auto &triangle = std::get<TriangleCoordinates>(coordinates);
        return {id, triangle.weights, triangle.gradients};
    }
};

export struct SurfaceHit
{
    SurfacePoint surface;
    double t;
};

// Optional counters owned by the caller, so the tree stays read-only between queries.
export struct RayQueryStats
{
    std::uint64_t rays{}, triangle_tests{};
    std::uint64_t sphere_tests{};
};

// Borrows the instances and snapshots face culling at build time. Rebuild if either changes.
export class SurfaceBvh
{
  public:
    SurfaceBvh() = default;
    SurfaceBvh(std::span<const MeshInstance> instances, std::span<const SphereInstance> spheres,
               SurfaceFilter accept = {}, const FaceCulling &culling = {});
    [[nodiscard]] bool empty() const noexcept
    {
        return nodes_.empty();
    }
    [[nodiscard]] std::optional<SurfaceHit> intersect_surface(const Ray &ray, RayInterval interval,
                                                              RayQueryStats *stats = nullptr) const;
    [[nodiscard]] bool occluded(const Ray &ray, RayInterval interval, RayQueryStats *stats = nullptr) const;

  private:
    struct Primitive
    {
        std::size_t instance{}, triangle{};
        CullFace cull{CullFace::None};
        bool sphere{};
    };
    struct BuildPrimitive;

    struct Node
    {
        Bounds3 bounds;
        // A leaf indexes primitives. An interior node's left child follows it immediately.
        std::size_t first_primitive_or_right_child, count;
    };
    std::span<const MeshInstance> instances_;
    std::span<const SphereInstance> spheres_;
    SurfaceFilter accept_;
    std::vector<Node> nodes_;
    std::vector<Primitive> primitives_;

    std::size_t build(std::span<BuildPrimitive> primitives);
    enum class Query
    {
        Closest,
        Any
    };
    template <Query Kind>
    bool trace_leaf(const Node &node, const Ray &ray, RayInterval interval, RayQueryStats *stats,
                    std::optional<SurfaceHit> &nearest) const;
    template <Query Kind>
    [[nodiscard]] std::optional<SurfaceHit> trace(const Ray &ray, RayInterval interval, RayQueryStats *stats) const;
};

export class TriangleBvh : public SurfaceBvh
{
  public:
    TriangleBvh() = default;
    explicit TriangleBvh(std::span<const MeshInstance> instances, SurfaceFilter accept = {},
                         const FaceCulling &culling = {})
        : SurfaceBvh(instances, {}, std::move(accept), culling)
    {
    }
    [[nodiscard]] std::optional<RayHit> intersect(const Ray &ray, RayInterval interval,
                                                  RayQueryStats *stats = nullptr) const
    {
        const auto hit = intersect_surface(ray, interval, stats);
        if (!hit)
        {
            return std::nullopt;
        }
        return RayHit{hit->surface.triangle(), hit->t};
    }
};

// Cache both sides of a hit when several light samples share the same receiver.
export class SurfaceRayOrigin
{
  public:
    SurfaceRayOrigin(const MeshInstance &instance, const Triangle &face, Barycentrics weights, double time = 0);
    SurfaceRayOrigin(const Sphere &sphere, Vec3 outward_normal, double time = 0);
    [[nodiscard]] Vec3 position() const noexcept
    {
        return position_;
    }

    [[nodiscard]] Ray along(Vec3 direction) const noexcept
    {
        return {dot(normal_, direction) < 0 ? negative_ : positive_, direction, time_};
    }
    [[nodiscard]] Vec3 geometric_normal() const noexcept
    {
        return normal_;
    }

    // The sampled endpoint is at t = 1 even after offsetting the origin.
    [[nodiscard]] Ray to(Vec3 endpoint) const noexcept
    {
        const Vec3 origin = along(endpoint - position_).origin;
        return {origin, endpoint - origin, time_};
    }

  private:
    double time_;
    Vec3 position_, normal_, positive_{}, negative_{};
    void offset_origins(Vec3 magnitudes);
};

SurfaceRayOrigin::SurfaceRayOrigin(const MeshInstance &instance, const Triangle &face, Barycentrics weights,
                                   double time)
    : time_(time), position_(instance.transform.to_world_point(face.position_at(weights))),
      normal_(instance.transform.to_world_normal(face.geometric_normal()))
{
    const auto &[a, b, c] = face.vertices;
    const Vec3 local_magnitudes = abs_components(a.position) + abs_components(b.position - a.position) * weights.b +
                                  abs_components(c.position - a.position) * weights.c;
    // Project a roundoff allowance onto the normal so distant tangential coordinates do not widen thin gaps.
    offset_origins(instance.transform.world_point_magnitudes(local_magnitudes));
}

SurfaceRayOrigin::SurfaceRayOrigin(const Sphere &sphere, Vec3 outward_normal, double time)
    : time_(time), position_(sphere.center_at(time) + outward_normal * sphere.radius), normal_(outward_normal)
{
    offset_origins(abs_components(sphere.center_at(time)) + abs_components(outward_normal) * sphere.radius);
}

void SurfaceRayOrigin::offset_origins(Vec3 magnitudes)
{
    const double scale = dot(abs_components(normal_), magnitudes);
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

// Mesh-space Möller–Trumbore intersection in the open interval (min, max).
export [[nodiscard]] std::optional<RayHit> intersect_triangle(const Ray &ray, const Triangle &triangle,
                                                              InstanceId instance, RayInterval interval,
                                                              const SurfaceFilter &accept,
                                                              CullFace cull = CullFace::None)
{
    const auto &[a, b, c] = triangle.vertices;
    const Vec3 ab = b.position - a.position;
    const Vec3 ac = c.position - a.position;
    const Vec3 p = cross(ray.direction, ac);
    const double determinant = dot(ab, p);
    if (determinant == 0.0 || culled(cull, determinant < 0.0))
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
    const TrianglePoint surface{{instance, triangle.id}, {1.0 - weight_b - weight_c, weight_b, weight_c}};
    if (accept && !accept(surface))
    {
        return std::nullopt;
    }
    return RayHit{surface, t};
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
                                                     RayInterval interval, const SurfaceFilter &accept = {},
                                                     const FaceCulling &culling = {})
{
    std::optional<RayHit> nearest;
    for (const auto &instance : instances)
    {
        const Ray local = instance.transform.to_local_ray(ray);
        for (const auto &triangle : instance.triangles)
        {
            const auto cull = culling ? culling({instance.id, triangle.id}) : CullFace::None;
            if (const auto hit = intersect_triangle(local, triangle, instance.id, interval, accept, cull))
            {
                keep_nearest(nearest, *hit);
            }
        }
    }
    return nearest;
}

// Any accepted hit in the open interval blocks the ray.
export [[nodiscard]] bool occluded(const Ray &ray, std::span<const MeshInstance> instances, RayInterval interval,
                                   const SurfaceFilter &accept = {}, const FaceCulling &culling = {})
{
    for (const auto &instance : instances)
    {
        const Ray local = instance.transform.to_local_ray(ray);
        for (const auto &triangle : instance.triangles)
        {
            const auto cull = culling ? culling({instance.id, triangle.id}) : CullFace::None;
            if (intersect_triangle(local, triangle, instance.id, interval, accept, cull))
            {
                return true;
            }
        }
    }
    return false;
}
} // namespace formlight
