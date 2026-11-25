export module formlight.ray_query;

import std;
export import formlight.triangle;

export namespace formlight
{
struct RayInterval
{
    double min, max;
};

struct RayHit
{
    TrianglePoint surface;
    double t;
};

// Two-sided Möller–Trumbore intersection in the open interval (min, max).
[[nodiscard]] std::optional<RayHit> intersect(const Ray &ray, const Triangle &triangle, RayInterval interval) noexcept
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
    return RayHit{.surface = {triangle.id, {1.0 - weight_b - weight_c, weight_b, weight_c}}, .t = t};
}
} // namespace formlight
