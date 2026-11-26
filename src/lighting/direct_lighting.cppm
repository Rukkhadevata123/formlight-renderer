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

export enum class ShadowState
{
    ZeroWeight,
    Disabled,
    Visible,
    Blocked
};

export struct ShadowResult
{
    ShadowState state{ShadowState::ZeroWeight};
    std::optional<SurfaceId> blocker{};
};

export struct LightSample
{
    std::optional<Vec3> position; // A directional light has no finite endpoint.
    double diffuse_weight;
    ShadowResult shadow;

    [[nodiscard]] double visible_weight() const noexcept
    {
        return shadow.state == ShadowState::Blocked || shadow.state == ShadowState::ZeroWeight ? 0.0 : diffuse_weight;
    }
};

export [[nodiscard]] ShadowResult trace_shadow(const TriangleBvh &blockers, const Ray &ray, RayInterval interval,
                                               bool identify_blocker) noexcept
{
    if (blockers.empty())
    {
        return {ShadowState::Disabled};
    }
    if (identify_blocker)
    {
        if (const auto hit = blockers.intersect(ray, interval))
        {
            return {ShadowState::Blocked, hit->surface.id};
        }
        return {ShadowState::Visible};
    }
    return {blockers.occluded(ray, interval) ? ShadowState::Blocked : ShadowState::Visible};
}

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
                                                const TriangleBvh &blockers, std::vector<LightSample> *record = nullptr,
                                                const std::stop_token &stop = {})
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
        if ((i % 256 == 0) && stop.stop_requested())
        {
            throw std::runtime_error("Rendering cancelled");
        }
        const double u = shifted_radical_inverse<2>(i, shift_u);
        const double v = shifted_radical_inverse<3>(i, shift_v);
        const Vec3 sample = light.sample(u, v);
        const double weight = light.diffuse_weight(receiver.position(), unit_normal, sample);
        const auto shadow =
            weight > 0 ? trace_shadow(blockers, receiver.to(sample), {0, 1}, record != nullptr) : ShadowResult{};
        const LightSample evaluated{sample, weight, shadow};
        sum += evaluated.visible_weight();
        if (record)
        {
            record->push_back(evaluated);
        }
    }
    return sum / sampling.samples;
}
} // namespace formlight
