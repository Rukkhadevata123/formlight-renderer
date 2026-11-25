export module formlight.geometry;

export namespace formlight
{
struct Vec3
{
    double x, y, z;
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

[[nodiscard]] constexpr Vec3 operator/(Vec3 v, double scale) noexcept
{
    return {v.x / scale, v.y / scale, v.z / scale};
}

[[nodiscard]] constexpr double dot(Vec3 a, Vec3 b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

struct Ray
{
    Vec3 origin;
    Vec3 direction;

    [[nodiscard]] constexpr Vec3 at(double t) const noexcept
    {
        return origin + direction * t;
    }
};
} // namespace formlight
