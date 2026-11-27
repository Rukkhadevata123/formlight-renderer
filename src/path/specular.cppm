export module formlight.specular;

import std;
import formlight.geometry;

export namespace formlight
{
enum class PathSurface
{
    Diffuse,
    Mirror,
    Dielectric
};

struct SpecularSample
{
    Vec3 direction;
    double radiance_weight;
    bool transmitted;
};

// Incident points toward the interface. The unit normal points into the incident medium.
[[nodiscard]] inline Vec3 mirror_direction(Vec3 incident, Vec3 normal) noexcept
{
    return incident - normal * (2 * dot(incident, normal));
}

[[nodiscard]] inline SpecularSample sample_dielectric(Vec3 incident, Vec3 normal, double eta_i_over_t,
                                                      double uniform) noexcept
{
    if (eta_i_over_t == 1)
    {
        return {incident, 1, true};
    }
    const double cos_i = std::clamp(-dot(incident, normal), 0.0, 1.0);
    const double sin2_t = eta_i_over_t * eta_i_over_t * (1 - cos_i) * (1 + cos_i);
    if (sin2_t < 1)
    {
        const double cos_t = std::sqrt(1 - sin2_t);
        const double rs = (eta_i_over_t * cos_i - cos_t) / (eta_i_over_t * cos_i + cos_t);
        const double rp = (cos_i - eta_i_over_t * cos_t) / (cos_i + eta_i_over_t * cos_t);
        const double fresnel = 0.5 * (rs * rs + rp * rp);
        if (uniform >= fresnel)
        {
            // The Fresnel branch probability cancels; radiance transport retains the eta-squared factor.
            return {(incident + normal * cos_i) * eta_i_over_t - normal * cos_t, eta_i_over_t * eta_i_over_t, true};
        }
    }
    return {mirror_direction(incident, normal), 1, false};
}
} // namespace formlight
