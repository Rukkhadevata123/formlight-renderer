export module formlight.direct_lighting;

import std;
import formlight.lighting;
import formlight.ray_query;

namespace formlight
{
export struct LightSampling
{
    std::uint32_t samples{16};
    std::uint64_t seed{};
};

namespace
{
// SplitMix64 finalizer for the per-pixel random shift.
std::uint64_t mix_bits(std::uint64_t value) noexcept
{
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

// Reflect the sample index's base digits across the radix point (Halton), then wrap the shift into [0, 1).
template <std::uint32_t Base> double shifted_radical_inverse(std::uint32_t index, double shift) noexcept
{
    double place = 1.0 / Base;
    while (index != 0)
    {
        shift += (index % Base) * place;
        index /= Base;
        place /= Base;
    }
    return shift - std::floor(shift);
}
} // namespace

export [[nodiscard]] double sample_area_diffuse(const RectangleLight &light, const SurfaceRayOrigin &receiver,
                                                Vec3 unit_normal, LightSampling sampling, std::uint64_t pixel,
                                                const TriangleBvh &blockers)
{
    if (sampling.samples == 0)
    {
        throw std::invalid_argument("Light sample count must be positive");
    }
    const std::uint64_t bits = mix_bits(mix_bits(pixel + 1) ^ sampling.seed);
    const double shift_u = static_cast<double>(bits >> 32) * 0x1p-32;
    const double shift_v = static_cast<double>(static_cast<std::uint32_t>(bits)) * 0x1p-32;
    double sum = 0;
    for (std::uint32_t i = 0; i < sampling.samples; ++i)
    {
        const double u = shifted_radical_inverse<2>(i, shift_u);
        const double v = shifted_radical_inverse<3>(i, shift_v);
        const Vec3 sample = light.sample(u, v);
        const double weight = light.diffuse_weight(receiver.position(), unit_normal, sample);
        if (weight > 0 && (blockers.empty() || !blockers.occluded(receiver.to(sample), {0, 1})))
        {
            sum += weight;
        }
    }
    return sum / sampling.samples;
}
} // namespace formlight
