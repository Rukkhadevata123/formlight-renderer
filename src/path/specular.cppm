export module formlight.specular;

import std;
import formlight.geometry;
import formlight.sampling;

export namespace formlight
{
enum class PathSurface
{
    Diffuse,
    Mirror,
    Dielectric,
    Metal
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

// Offset the ideal reflection by a point on the surface of a sphere of radius fuzz.
[[nodiscard]] inline std::optional<Vec3> sample_metal(Vec3 reflected, Vec3 geometric, double fuzz, PathSampler &sampler)
{
    if (fuzz == 0)
    {
        return reflected;
    }
    const double u = sampler.uniform();
    const Vec3 scattered = reflected + uniform_sphere(u, sampler.uniform()) * fuzz;
    if (dot(geometric, scattered) <= 0)
    {
        return std::nullopt;
    }
    return unit_vector(scattered, "Invalid metal scattering direction");
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
