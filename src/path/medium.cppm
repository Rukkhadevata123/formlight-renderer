export module formlight.medium;

import std;
export import formlight.sphere;
import formlight.sampling;

export namespace formlight
{
struct HomogeneousMedium
{
    Sphere boundary;
    double extinction{1}; // Inverse world distance.
    Vec3 scattering_albedo{}; // Per-channel fraction of extinct light scattered at a collision.

    void fields(this auto &self, auto &&field)
    {
        field("center", self.boundary.center);
        field("radius", self.boundary.radius);
        field("displacement", self.boundary.displacement);
        field("extinction", self.extinction);
        field("scattering_albedo", self.scattering_albedo);
    }

    void validate() const
    {
        (void)boundary.bounds();
        if (!(extinction >= 0 && std::isfinite(extinction)))
        {
            throw std::invalid_argument("Medium extinction must be finite and nonnegative");
        }
        for (const double fraction : {scattering_albedo.x, scattering_albedo.y, scattering_albedo.z})
        {
            if (!(fraction >= 0 && fraction <= 1))
            {
                throw std::invalid_argument("Medium scattering_albedo must be in [0, 1]");
            }
        }
    }

    [[nodiscard]] std::optional<RayInterval> segment(const Ray &ray, RayInterval interval) const
    {
        if (extinction == 0)
        {
            return std::nullopt;
        }
        const auto chord = sphere_chord(ray, boundary);
        if (!chord)
        {
            return std::nullopt;
        }
        const double enter = std::max({0.0, interval.min, chord->roots.min});
        const double leave = std::min(interval.max, chord->roots.max);
        if (enter >= leave)
        {
            return std::nullopt;
        }
        return RayInterval{enter, leave};
    }
};

struct MediumHit
{
    std::size_t index;
    double t;
    Vec3 scattering_albedo{};
};

[[nodiscard]] inline std::optional<MediumHit> sample_free_flight(std::span<const HomogeneousMedium> media,
                                                                 const Ray &ray, RayInterval interval, PathSampler &rng)
{
    std::optional<MediumHit> nearest;
    const double speed = std::hypot(ray.direction.x, ray.direction.y, ray.direction.z);
    for (std::size_t i = 0; i < media.size(); ++i)
    {
        if (const auto inside = media[i].segment(ray, interval))
        {
            const double distance = -std::log1p(-rng.uniform()) / media[i].extinction;
            const double t = inside->min + distance / speed;
            if (t < inside->max)
            {
                nearest = MediumHit{i, t, media[i].scattering_albedo};
                interval.max = t;
            }
        }
    }
    return nearest;
}

[[nodiscard]] inline double medium_transmittance(std::span<const HomogeneousMedium> media, const Ray &ray,
                                                 RayInterval interval)
{
    const double speed = std::hypot(ray.direction.x, ray.direction.y, ray.direction.z);
    double optical_depth = 0;
    for (const auto &medium : media)
    {
        if (const auto inside = medium.segment(ray, interval))
        {
            optical_depth += medium.extinction * ((inside->max - inside->min) * speed);
        }
    }
    return std::exp(-optical_depth);
}
} // namespace formlight
