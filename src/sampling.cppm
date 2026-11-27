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

// Map a uniform disk to the hemisphere. The resulting solid-angle density is cos(theta) / pi.
[[nodiscard]] inline Vec3 cosine_hemisphere(Vec3 normal, double u, double v)
{
    const double radius = std::sqrt(u);
    const double phi = 2 * std::numbers::pi * v;
    const Vec3 axis = std::abs(normal.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    const Vec3 tangent = unit_vector(cross(axis, normal), "Invalid scattering normal");
    return tangent * (radius * std::cos(phi)) + cross(normal, tangent) * (radius * std::sin(phi)) +
           normal * std::sqrt(1 - u);
}
} // namespace formlight
