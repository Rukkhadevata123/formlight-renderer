export module formlight.sampling;

import std;
import formlight.geometry;

export namespace formlight
{
[[nodiscard]] constexpr std::uint64_t splitmix64_finalize(std::uint64_t value) noexcept
{
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

class PathSampler
{
  public:
    PathSampler(std::uint64_t seed, std::uint64_t pixel, std::uint32_t sample)
        : state_(splitmix64_finalize(splitmix64_finalize(pixel + 1) ^ seed) + sample)
    {
    }

    std::uint64_t next_bits() noexcept
    {
        state_ += 0x9e3779b97f4a7c15ULL;
        return splitmix64_finalize(state_);
    }
    double uniform() noexcept
    {
        return static_cast<double>(next_bits() >> 11) * 0x1p-53;
    }

  private:
    std::uint64_t state_;
};

[[nodiscard]] inline Vec3 polar_direction(Vec3 normal, double sin_theta, double cos_theta, double phi)
{
    const Vec3 axis = std::abs(normal.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    const Vec3 tangent = unit_vector(cross(axis, normal), "Invalid scattering normal");
    return tangent * (sin_theta * std::cos(phi)) + cross(normal, tangent) * (sin_theta * std::sin(phi)) +
           normal * cos_theta;
}

[[nodiscard]] inline Vec3 uniform_sphere(double u, double v)
{
    const double z = 1 - 2 * u;
    const double radius = std::sqrt((1 - z) * (1 + z));
    const double phi = 2 * std::numbers::pi * v;
    return {radius * std::cos(phi), radius * std::sin(phi), z};
}

// Map a uniform disk to the hemisphere. The resulting solid-angle density is cos(theta) / pi.
[[nodiscard]] inline Vec3 cosine_hemisphere(Vec3 normal, double u, double v)
{
    return polar_direction(normal, std::sqrt(u), std::sqrt(1 - u), 2 * std::numbers::pi * v);
}
} // namespace formlight
