export module formlight.sphere;

import std;
export import formlight.mesh_instance;

export namespace formlight
{
struct Sphere
{
    Vec3 center{};
    double radius{1};
    Vec3 displacement{}; // From normalized time 0 to 1.

    [[nodiscard]] Vec3 center_at(double time) const noexcept
    {
        return center + displacement * time;
    }

    [[nodiscard]] Bounds3 bounds() const
    {
        const Vec3 extent{radius, radius, radius};
        const Vec3 end = center_at(1);
        if (!std::isfinite(end.x) || !std::isfinite(end.y) || !std::isfinite(end.z))
        {
            throw std::invalid_argument("Sphere motion requires a finite end position");
        }
        Bounds3 box{center - extent, center + extent};
        box.include(end - extent);
        box.include(end + extent);
        const double extent_radius = box.radius();
        if (!(radius > 0 && std::isfinite(radius) && extent_radius > 0 && std::isfinite(extent_radius)))
        {
            throw std::invalid_argument("Sphere needs a finite center and positive finite radius");
        }
        return box;
    }
};

struct SphereInstance
{
    InstanceId id{};
    Sphere shape;
    std::size_t material_index{};
    bool sampling_target{};
};

struct SphereIntersection
{
    double t;
    Vec3 outward_normal;
};

[[nodiscard]] inline std::optional<SphereIntersection> intersect_sphere(const Ray &ray, const Sphere &sphere,
                                                                        RayInterval interval)
{
    const double speed = std::hypot(ray.direction.x, ray.direction.y, ray.direction.z);
    if (!(speed > 0 && std::isfinite(speed)))
    {
        return std::nullopt;
    }
    const Vec3 direction = ray.direction / speed;
    const Vec3 to_center = sphere.center_at(ray.time) - ray.origin;
    const double projection = dot(to_center, direction);
    const Vec3 cross_distance = cross(to_center, direction);
    const double perpendicular = std::hypot(cross_distance.x, cross_distance.y, cross_distance.z);
    if (perpendicular > sphere.radius)
    {
        return std::nullopt;
    }
    // Half-chord length in world units. The factored difference preserves near-tangent hits.
    const double half_chord = sphere.radius * std::sqrt(((sphere.radius - perpendicular) / sphere.radius) *
                                                        (1 + perpendicular / sphere.radius));
    for (const double offset : {-half_chord, half_chord})
    {
        double distance = projection + offset;
        if (std::abs(distance) < 0.5 * std::abs(offset))
        {
            // Near the surface, recover the shorter root from the product of the two roots.
            const double length = std::hypot(to_center.x, to_center.y, to_center.z);
            const double other = projection - offset;
            distance = (length - sphere.radius) * (length / other + sphere.radius / other);
        }
        const double t = distance / speed;
        if (t > interval.min && t < interval.max)
        {
            // Construct the normal about closest approach, avoiding cancellation for distant origins.
            const Vec3 radial = cross(cross_distance, direction) + direction * offset;
            return SphereIntersection{t, unit_vector(radial, "Invalid sphere intersection normal")};
        }
    }
    return std::nullopt;
}
} // namespace formlight
