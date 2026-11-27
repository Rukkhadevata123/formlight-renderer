export module formlight.camera;

import std;
export import formlight.geometry;
export import formlight.image;

namespace formlight
{
// Continuous pixel coordinates, measured right and down from the top-left image corner.
export struct ImagePoint
{
    double x, y;
};

export [[nodiscard]] constexpr ImagePoint pixel_center(int column, int row) noexcept
{
    return {column + 0.5, row + 0.5};
}

export struct CameraView
{
    Vec3 position;
    Vec3 target;
    Vec3 up;
    double vertical_fov_radians;
};

export struct ProjectedPoint
{
    ImagePoint image;
    double view_depth; // Positive distance along the camera's forward axis (-Z in view space).
};

// Camera-space coordinates before perspective division. Depth is positive along the view direction.
export struct ViewPoint
{
    double x, y, depth;
};

export struct ViewDepthRange
{
    double near_depth, far_depth;

    [[nodiscard]] constexpr bool contains(double depth) const noexcept
    {
        return depth >= near_depth && depth <= far_depth;
    }
};

export class PinholeCamera
{
  public:
    PinholeCamera(const CameraView &view, ImageExtent extent);
    [[nodiscard]] CameraView view() const noexcept
    {
        return view_;
    }
    [[nodiscard]] ImageExtent extent() const noexcept
    {
        return extent_;
    }

    // Return a finite projection in front of the eye plane, including points outside the image.
    [[nodiscard]] std::optional<ProjectedPoint> project(Vec3 world_point) const noexcept;
    [[nodiscard]] ViewPoint to_view(Vec3 world_point) const noexcept;
    [[nodiscard]] double forward_component(Vec3 direction) const noexcept
    {
        return dot(direction, forward_);
    }
    [[nodiscard]] std::optional<ProjectedPoint> project_view(ViewPoint point) const noexcept;
    // The primary ray has unit direction, so its parameter measures distance in world units.
    [[nodiscard]] Ray ray_for_sample(ImagePoint sample) const noexcept;

  private:
    CameraView view_;
    Vec3 forward_{}, right_{}, up_{};
    ImageExtent extent_;
    double focal_length_pixels_{};
};

PinholeCamera::PinholeCamera(const CameraView &view, ImageExtent extent) : view_(view), extent_(extent)
{
    if (extent.width <= 0 || extent.height <= 0)
    {
        throw std::invalid_argument("Image dimensions must be positive");
    }
    if (!(view.vertical_fov_radians > 0.0 && view.vertical_fov_radians < std::numbers::pi))
    {
        throw std::invalid_argument("Vertical field of view must be between zero and pi radians");
    }
    focal_length_pixels_ = (extent.height * 0.5) / std::tan(view.vertical_fov_radians * 0.5);
    if (!std::isfinite(focal_length_pixels_))
    {
        throw std::invalid_argument("Vertical field of view is too small for this image size");
    }

    forward_ = unit_vector(view.target - view.position, "Camera position and target must define a finite direction");
    const Vec3 up_hint = unit_vector(view.up, "Camera up must be finite and nonzero");
    const Vec3 side = cross(forward_, up_hint);
    const double sine = std::sqrt(dot(side, side));
    // A cross product at roundoff scale cannot reliably determine camera roll.
    if (sine <= 16.0 * std::numeric_limits<double>::epsilon())
    {
        throw std::invalid_argument("Camera up is parallel to the view direction");
    }
    right_ = side * (1.0 / sine);
    up_ = cross(right_, forward_);
}

std::optional<ProjectedPoint> PinholeCamera::project(Vec3 world_point) const noexcept
{
    return project_view(to_view(world_point));
}

ViewPoint PinholeCamera::to_view(Vec3 world_point) const noexcept
{
    const Vec3 offset = world_point - view_.position;
    return {dot(offset, right_), dot(offset, up_), dot(offset, forward_)};
}

std::optional<ProjectedPoint> PinholeCamera::project_view(ViewPoint point) const noexcept
{
    if (!(point.depth > 0.0) || !std::isfinite(point.depth))
    {
        return std::nullopt;
    }

    const double x = extent_.width * 0.5 + focal_length_pixels_ * (point.x / point.depth);
    const double y = extent_.height * 0.5 - focal_length_pixels_ * (point.y / point.depth);
    if (!std::isfinite(x) || !std::isfinite(y))
    {
        return std::nullopt;
    }
    return ProjectedPoint{.image = {x, y}, .view_depth = point.depth};
}

Ray PinholeCamera::ray_for_sample(ImagePoint sample) const noexcept
{
    const double x = (sample.x - extent_.width * 0.5) / focal_length_pixels_;
    const double y = (extent_.height * 0.5 - sample.y) / focal_length_pixels_;
    const Vec3 direction = forward_ + right_ * x + up_ * y;
    return {.origin = view_.position, .direction = direction * (1.0 / std::sqrt(dot(direction, direction)))};
}
} // namespace formlight
