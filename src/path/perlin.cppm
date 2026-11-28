export module formlight.perlin;

import std;
import formlight.geometry;
import formlight.sampling;

export namespace formlight
{
class PerlinNoise
{
  public:
    explicit PerlinNoise(std::uint64_t seed)
    {
        std::iota(permutation_.begin(), permutation_.end(), 0);
        PathSampler random{seed, 0, 0};
        for (unsigned count = 256; count > 1; --count)
        {
            const auto index = static_cast<unsigned>(random.uniform() * count);
            std::swap(permutation_[count - 1], permutation_[index]);
        }
    }

    [[nodiscard]] double noise(Vec3 point) const noexcept
    {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
        {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const auto x = axis(point.x), y = axis(point.y), z = axis(point.z);
        std::array<double, 2> planes{};
        for (unsigned dz = 0; dz < 2; ++dz)
        {
            const unsigned z_hash = permutation_[(z.cell + dz) & 255u];
            std::array<double, 2> rows{};
            for (unsigned dy = 0; dy < 2; ++dy)
            {
                const unsigned yz = permutation_[(y.cell + dy + z_hash) & 255u];
                const auto corner = [&](unsigned dx)
                {
                    const auto hash = permutation_[(x.cell + dx + yz) & 255u];
                    const Vec3 offset{x.offset - dx, y.offset - dy, z.offset - dz};
                    return dot(gradients_[hash % gradients_.size()], offset) / std::numbers::sqrt2;
                };
                rows[dy] = std::lerp(corner(0), corner(1), x.fade);
            }
            planes[dz] = std::lerp(rows[0], rows[1], y.fade);
        }
        return std::lerp(planes[0], planes[1], z.fade);
    }

    [[nodiscard]] double turbulence(Vec3 point) const noexcept
    {
        // The lattice repeats every 256 units. Reduce once before doubling the coordinates.
        point = {std::fmod(point.x, 256.0), std::fmod(point.y, 256.0), std::fmod(point.z, 256.0)};
        double sum = 0, weight = 1;
        for (unsigned octave = 0; octave < 7; ++octave)
        {
            sum += weight * noise(point);
            point = point * 2;
            weight *= 0.5;
        }
        return std::abs(sum);
    }

  private:
    struct Axis
    {
        unsigned cell;
        double offset, fade;
    };

    static Axis axis(double value) noexcept
    {
        const double base = std::floor(value);
        const double t = value - base;
        const int cell = static_cast<int>(std::fmod(base, 256.0));
        return {static_cast<unsigned>(cell) & 255u, t, t * t * t * (t * (6 * t - 15) + 10)};
    }

    static constexpr std::array<Vec3, 12> gradients_{{{1, 1, 0},
                                                      {-1, 1, 0},
                                                      {1, -1, 0},
                                                      {-1, -1, 0},
                                                      {1, 0, 1},
                                                      {-1, 0, 1},
                                                      {1, 0, -1},
                                                      {-1, 0, -1},
                                                      {0, 1, 1},
                                                      {0, -1, 1},
                                                      {0, 1, -1},
                                                      {0, -1, -1}}};
    std::array<std::uint8_t, 256> permutation_{};
};

struct MarbleTexture
{
    PerlinNoise noise;
    double scale{};

    [[nodiscard]] double sample(Vec3 point) const noexcept
    {
        return 0.5 * (1 + std::sin(scale * point.z + 10 * noise.turbulence(point)));
    }
};
} // namespace formlight
