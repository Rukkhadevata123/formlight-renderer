export module formlight.lighting;

import std;
import formlight.geometry;
import formlight.image;

export namespace formlight
{
// White directional light, scaled so a surface facing the light returns its base color.
class DirectionalLight
{
  public:
    explicit DirectionalLight(Vec3 toward_light)
        : toward_light_(unit_vector(toward_light, "light.toward_light must be finite and nonzero"))
    {
    }

    [[nodiscard]] Vec3 toward_light() const noexcept
    {
        return toward_light_;
    }

    [[nodiscard]] LinearColor diffuse(LinearColor base_color, Vec3 unit_world_normal) const noexcept
    {
        const auto cosine = static_cast<float>(std::max(0.0, dot(unit_world_normal, toward_light_)));
        return {base_color.r * cosine, base_color.g * cosine, base_color.b * cosine, base_color.a};
    }

  private:
    Vec3 toward_light_;
};

// One-sided white Lambertian emitter. The two full edge vectors determine size and emitting side.
class RectangleLight
{
  public:
    RectangleLight(Vec3 center, Vec3 edge_u, Vec3 edge_v, double power);
    [[nodiscard]] Vec3 sample(double u, double v) const noexcept
    {
        return center_ + edge_u_ * (u - 0.5) + edge_v_ * (v - 0.5);
    }
    [[nodiscard]] double diffuse_weight(Vec3 receiver, Vec3 unit_normal, Vec3 light_point) const noexcept;

  private:
    Vec3 center_, edge_u_, edge_v_, normal_{};
    double diffuse_scale_;
};

RectangleLight::RectangleLight(Vec3 center, Vec3 edge_u, Vec3 edge_v, double power)
    : center_(center), edge_u_(edge_u), edge_v_(edge_v), diffuse_scale_(power / (std::numbers::pi * std::numbers::pi))
{
    if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z))
    {
        throw std::invalid_argument("light.center must be finite");
    }
    if (!std::isfinite(power) || power < 0)
    {
        throw std::invalid_argument("light.power must be finite and nonnegative");
    }
    const Vec3 u = unit_vector(edge_u, "Rectangle light width must be finite and nonzero");
    const Vec3 v = unit_vector(edge_v, "Rectangle light height must be finite and nonzero");
    if (std::abs(dot(u, v)) > 1e-10)
    {
        throw std::invalid_argument("Rectangle light edges must be perpendicular");
    }
    normal_ = unit_vector(cross(u, v), "Invalid rectangle light normal");
}

double RectangleLight::diffuse_weight(Vec3 receiver, Vec3 unit_normal, Vec3 light_point) const noexcept
{
    const Vec3 delta = light_point - receiver;
    const double distance_squared = dot(delta, delta);
    if (!(distance_squared > 0) || !std::isfinite(distance_squared))
    {
        return 0;
    }
    // Uniform area sampling cancels the A in Le = power / (pi * A). Lambert's BRDF adds the second 1/pi.
    // Each dot uses an unnormalized direction, so their product contains another factor of distance squared.
    const double receiver_cos_over_r = std::max(0.0, dot(unit_normal, delta)) / distance_squared;
    const double emitter_cos_over_r = std::max(0.0, -dot(normal_, delta)) / distance_squared;
    return diffuse_scale_ * receiver_cos_over_r * emitter_cos_over_r;
}
} // namespace formlight
