export module formlight.geometry;

import std;

export namespace formlight
{
struct Vec3
{
    double x, y, z;

    bool operator==(const Vec3 &) const = default;
};

[[nodiscard]] constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] constexpr Vec3 operator*(Vec3 v, double scale) noexcept
{
    return {v.x * scale, v.y * scale, v.z * scale};
}

[[nodiscard]] constexpr Vec3 component_product(Vec3 a, Vec3 b) noexcept
{
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

[[nodiscard]] constexpr Vec3 operator/(Vec3 v, double scale) noexcept
{
    return {v.x / scale, v.y / scale, v.z / scale};
}

[[nodiscard]] constexpr Vec3 abs_components(Vec3 v) noexcept
{
    return {std::abs(v.x), std::abs(v.y), std::abs(v.z)};
}

[[nodiscard]] constexpr double dot(Vec3 a, Vec3 b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] inline Vec3 unit_vector(Vec3 value, const char *error)
{
    const double length = std::hypot(value.x, value.y, value.z);
    if (!(length > 0.0) || !std::isfinite(length))
    {
        throw std::invalid_argument(error);
    }
    return value / length;
}

struct Ray
{
    Vec3 origin{};
    Vec3 direction{};
    double time{}; // Normalized scene time shared by all rays of a path.

    [[nodiscard]] constexpr Vec3 at(double t) const noexcept
    {
        return origin + direction * t;
    }
};

struct RayInterval
{
    double min, max;
};
} // namespace formlight
