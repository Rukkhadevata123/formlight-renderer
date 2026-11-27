export module formlight.lighting;

import std;
import formlight.geometry;

export namespace formlight
{
struct IncidentLight
{
    Vec3 direction{};
    Vec3 weight{}; // Linear RGB radiance including the light-sampling measure.
};

[[nodiscard]] inline bool finite_nonnegative_components(Vec3 value) noexcept
{
    const auto valid = [](double component) { return std::isfinite(component) && component >= 0; };
    return valid(value.x) && valid(value.y) && valid(value.z);
}

class DirectionalLight
{
  public:
    // A unit Lambertian reflector under default white light has unit brightness at normal incidence.
    static constexpr double default_intensity = std::numbers::pi;
    explicit DirectionalLight(Vec3 toward_light, Vec3 color = {1, 1, 1}, double intensity = default_intensity);

    [[nodiscard]] Vec3 toward_light() const noexcept
    {
        return incident_.direction;
    }

    [[nodiscard]] IncidentLight incident() const noexcept
    {
        return incident_;
    }

  private:
    IncidentLight incident_;
};

[[nodiscard]] inline Vec3 light_radiance(Vec3 color, double intensity)
{
    if (!finite_nonnegative_components(color))
    {
        throw std::invalid_argument("light.color requires three finite, nonnegative channels");
    }
    if (!std::isfinite(intensity) || intensity < 0)
    {
        throw std::invalid_argument("light.intensity must be finite and nonnegative");
    }
    const Vec3 radiance = color * intensity;
    if (!finite_nonnegative_components(radiance))
    {
        throw std::invalid_argument("light.color times light.intensity must be finite");
    }
    return radiance;
}

DirectionalLight::DirectionalLight(Vec3 toward_light, Vec3 color, double intensity)
    : incident_{unit_vector(toward_light, "light.toward_light must be finite and nonzero"),
                light_radiance(color, intensity)}
{
}

class PointLight
{
  public:
    static constexpr Vec3 default_attenuation{1, 0.09, 0.032};
    PointLight(Vec3 position, Vec3 color, double intensity, Vec3 attenuation = default_attenuation)
        : position_(position), radiance_(light_radiance(color, intensity)), attenuation_(attenuation)
    {
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
        {
            throw std::invalid_argument("light.position must be finite");
        }
        if (!finite_nonnegative_components(attenuation) || attenuation == Vec3{})
        {
            throw std::invalid_argument(
                "light.attenuation requires finite, nonnegative coefficients with at least one positive value");
        }
    }

    [[nodiscard]] Vec3 position() const noexcept
    {
        return position_;
    }
    [[nodiscard]] IncidentLight incident(Vec3 receiver) const noexcept;

  private:
    Vec3 position_, radiance_, attenuation_;
};

IncidentLight PointLight::incident(Vec3 receiver) const noexcept
{
    const Vec3 delta = position_ - receiver;
    const double distance = std::hypot(delta.x, delta.y, delta.z);
    if (distance == 0 || !std::isfinite(distance))
    {
        return {};
    }
    const auto [constant, linear, quadratic] = attenuation_;
    const double denominator = constant + distance * (linear + distance * quadratic);
    if (denominator == 0)
    {
        return {};
    }
    return {delta / distance, radiance_ / denominator};
}

// One-sided white Lambertian emitter. The two full edge vectors determine size and emitting side.
class RectangleLight
{
  public:
    RectangleLight(Vec3 center, Vec3 edge_u, Vec3 edge_v, double power);
    [[nodiscard]] Vec3 sample(double u, double v) const noexcept
    {
        return center_ + edge_u_ * (u - 0.5) + edge_v_ * (v - 0.5);
    }
    [[nodiscard]] IncidentLight incident(Vec3 receiver, Vec3 light_point) const noexcept;

  private:
    Vec3 center_, edge_u_, edge_v_, normal_{};
    double radiance_times_area_;
};

RectangleLight::RectangleLight(Vec3 center, Vec3 edge_u, Vec3 edge_v, double power)
    : center_(center), edge_u_(edge_u), edge_v_(edge_v), radiance_times_area_(power / std::numbers::pi)
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

IncidentLight RectangleLight::incident(Vec3 receiver, Vec3 light_point) const noexcept
{
    const Vec3 delta = light_point - receiver;
    const double distance_squared = dot(delta, delta);
    if (!(distance_squared > 0) || !std::isfinite(distance_squared))
    {
        return {};
    }
    // Uniform area sampling cancels the area in Le = power / (pi * area).
    const Vec3 direction = delta / std::sqrt(distance_squared);
    const double emitter_cosine = std::max(0.0, -dot(normal_, direction));
    const double weight = radiance_times_area_ * emitter_cosine / distance_squared;
    return {direction, {weight, weight, weight}};
}
} // namespace formlight
