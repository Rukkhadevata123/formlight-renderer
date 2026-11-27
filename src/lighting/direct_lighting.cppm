export module formlight.direct_lighting;

import std;
import formlight.lighting;
import formlight.material;
import formlight.ray_query;

namespace formlight
{
export struct LightSampling
{
    std::uint32_t samples{16};
    std::uint64_t seed{};
};

export enum class AreaLightPattern
{
    ShiftedHalton,
    Center
};

export enum class ShadowState
{
    ZeroWeight,
    Disabled,
    Visible,
    Blocked,
    PartiallyVisible
};

export struct ShadowResult
{
    ShadowState state{ShadowState::ZeroWeight};
    std::optional<SurfaceId> blocker{};
    double visibility{state == ShadowState::Blocked || state == ShadowState::ZeroWeight ? 0.0 : 1.0};
};

export struct LightSample
{
    std::optional<Vec3> position; // A directional light has no finite endpoint.
    Vec3 direct;                  // Visible, linear RGB before sample averaging or layer compositing.
    ShadowResult shadow;
};

export enum class LightKind
{
    Directional,
    Point,
    Rectangle
};

export struct DiagnosticLight
{
    std::size_t scene_index{};
    LightKind kind{};
    std::uint64_t sampling_seed{};
    std::vector<LightSample> samples{};
    Vec3 direct{}; // Mean visible direct light on the selected surface, before opacity.
};

export [[nodiscard]] LightSample evaluate_light_sample(const MetallicRoughnessBrdf &brdf, IncidentLight incident,
                                                       std::optional<Vec3> position, auto visibility)
{
    if (incident.weight == Vec3{})
    {
        return {position, {}, {}};
    }
    const auto response = brdf.evaluate_times_cosine(incident.direction);
    const Vec3 direct{response.x * incident.weight.x, response.y * incident.weight.y, response.z * incident.weight.z};
    if (direct == Vec3{})
    {
        return {position, {}, {}};
    }
    const ShadowResult shadow = visibility();
    return {position, shadow.visibility == 0 ? Vec3{} : direct * shadow.visibility, shadow};
}

export [[nodiscard]] ShadowResult trace_shadow(const TriangleBvh &blockers, const Ray &ray, RayInterval interval,
                                               bool identify_blocker)
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

export [[nodiscard]] LightSample evaluate_finite_light_sample(const MetallicRoughnessBrdf &brdf, IncidentLight incident,
                                                              const SurfaceRayOrigin &receiver, Vec3 endpoint,
                                                              const TriangleBvh *blockers, bool identify_blocker)
{
    const auto visibility = [&]
    {
        return blockers ? trace_shadow(*blockers, receiver.to(endpoint), {0, 1}, identify_blocker)
                        : ShadowResult{ShadowState::Disabled};
    };
    return evaluate_light_sample(brdf, incident, endpoint, visibility);
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

export [[nodiscard]] Vec3 sample_area_light(const RectangleLight &light, const SurfaceRayOrigin &receiver,
                                            const MetallicRoughnessBrdf &brdf, LightSampling sampling,
                                            std::uint64_t pixel, const TriangleBvh *blockers,
                                            AreaLightPattern pattern = AreaLightPattern::ShiftedHalton,
                                            std::vector<LightSample> *record = nullptr,
                                            const std::stop_token &stop = {})
{
    if (sampling.samples == 0)
    {
        throw std::invalid_argument("Light sample count must be positive");
    }
    // Halton sample zero is exactly its shift. A shared midpoint gives navigation a stable hard shadow.
    double shift_u = 0.5, shift_v = 0.5;
    std::uint32_t count = 1;
    if (pattern == AreaLightPattern::ShiftedHalton)
    {
        const std::uint64_t bits = mix_bits(mix_bits(pixel + 1) ^ sampling.seed);
        shift_u = static_cast<double>(bits >> 32) * 0x1p-32;
        shift_v = static_cast<double>(static_cast<std::uint32_t>(bits)) * 0x1p-32;
        count = sampling.samples;
    }
    Vec3 sum{};
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if ((i % 256 == 0) && stop.stop_requested())
        {
            throw std::runtime_error("Rendering cancelled");
        }
        const double u = shifted_radical_inverse<2>(i, shift_u);
        const double v = shifted_radical_inverse<3>(i, shift_v);
        const Vec3 sample = light.sample(u, v);
        const auto incident = light.incident(receiver.position(), sample);
        const auto evaluated =
            evaluate_finite_light_sample(brdf, incident, receiver, sample, blockers, record != nullptr);
        sum = sum + evaluated.direct;
        if (record)
        {
            record->push_back(evaluated);
        }
    }
    return sum / count;
}

export struct AreaLighting
{
    RectangleLight light;
    LightSampling sampling{};
    bool shadows{true};

    [[nodiscard]] Vec3 evaluate(const SurfaceRayOrigin &receiver, const MetallicRoughnessBrdf &brdf,
                                std::uint64_t pixel, const TriangleBvh *blockers, AreaLightPattern pattern,
                                DiagnosticLight *record, std::stop_token stop) const
    {
        if (record)
        {
            record->kind = LightKind::Rectangle;
            record->sampling_seed = sampling.seed;
        }
        return sample_area_light(light, receiver, brdf, sampling, pixel, shadows ? blockers : nullptr, pattern,
                                 record ? &record->samples : nullptr, stop);
    }
};
} // namespace formlight
