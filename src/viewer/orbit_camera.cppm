export module formlight.viewer.orbit_camera;

import std;
import formlight.preview;

namespace formlight
{
export class OrbitCamera
{
  public:
    explicit OrbitCamera(const PinholeCamera &camera);
    void orbit(double dx, double dy);
    // Drag in fractions of the displayed image height, in both axes.
    void pan(double dx, double dy);
    void zoom(double notches);
    void focus(const Bounds3 &instance);
    void resize(ImageExtent extent)
    {
        extent_ = extent;
    }
    [[nodiscard]] PreviewView view(const Bounds3 &scene) const;

  private:
    struct Axes
    {
        Vec3 back, right, up;
    };
    [[nodiscard]] Axes axes() const;

    ImageExtent extent_;
    double fov_, azimuth_{}, elevation_{}, distance_{}, reference_distance_{};
    Vec3 target_;
    double roll_sin_{}, roll_cos_{};
};

OrbitCamera::OrbitCamera(const PinholeCamera &camera)
    : extent_(camera.extent()), fov_(camera.view().vertical_fov_radians), target_(camera.view().target)
{
    const Vec3 offset = camera.view().position - target_;
    reference_distance_ = distance_ = std::hypot(offset.x, offset.y, offset.z);
    const Vec3 back = offset / distance_;
    const Vec3 right =
        unit_vector(cross(unit_vector(camera.view().up, "Invalid camera up"), back), "Invalid camera axes");
    const double horizontal = std::hypot(back.x, back.z);
    azimuth_ = horizontal > 0 ? std::atan2(back.x, back.z) : std::atan2(-right.z, right.x);
    elevation_ = std::atan2(back.y, horizontal);

    // Preserve the authored roll, including for a camera looking straight down.
    const Vec3 level_right{std::cos(azimuth_), 0, -std::sin(azimuth_)};
    const Vec3 up = cross(back, right);
    roll_sin_ = dot(up, level_right);
    roll_cos_ = dot(up, cross(back, level_right));
}

OrbitCamera::Axes OrbitCamera::axes() const
{
    const double sine = std::sin(azimuth_), cosine = std::cos(azimuth_);
    const double horizontal = std::cos(elevation_);
    const Vec3 back{horizontal * sine, std::sin(elevation_), horizontal * cosine};
    const Vec3 level_right{cosine, 0, -sine};
    const Vec3 up = level_right * roll_sin_ + cross(back, level_right) * roll_cos_;
    return {back, cross(up, back), up};
}

void OrbitCamera::orbit(double dx, double dy)
{
    constexpr double radians_per_pixel = 0.35 * std::numbers::pi / 180;
    constexpr double limit = 89 * std::numbers::pi / 180;
    azimuth_ = std::remainder(azimuth_ - dx * radians_per_pixel, 2 * std::numbers::pi);
    elevation_ =
        std::clamp(elevation_ + dy * radians_per_pixel, std::min(-limit, elevation_), std::max(limit, elevation_));
}

void OrbitCamera::pan(double dx, double dy)
{
    const auto [back, right, up] = axes();
    const double height = 2 * distance_ * std::tan(fov_ / 2);
    target_ = target_ + (up * dy - right * dx) * height;
}

void OrbitCamera::zoom(double notches)
{
    const auto p = abs_components(target_);
    const double precision = 8 * std::numeric_limits<double>::epsilon() * std::max({p.x, p.y, p.z});
    const double minimum = std::max(reference_distance_ * 1e-3, precision);
    distance_ = std::clamp(distance_ * std::pow(1.15, -notches), minimum, std::max(minimum, reference_distance_ * 1e3));
}

void OrbitCamera::focus(const Bounds3 &instance)
{
    target_ = instance.center();
    reference_distance_ = distance_ = framing_distance(instance.radius(), extent_, fov_);
}

PreviewView OrbitCamera::view(const Bounds3 &scene) const
{
    const auto basis = axes();
    const PinholeCamera camera{{target_ + basis.back * distance_, target_, basis.up, fov_}, extent_};
    return clip_to_bounds(camera, scene, distance_ * 1e-5);
}
} // namespace formlight
