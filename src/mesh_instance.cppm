export module formlight.mesh_instance;

import std;
export import formlight.triangle;

namespace formlight
{
export enum class InstanceId : std::uint32_t
{
};

export struct SurfaceId
{
    InstanceId instance;
    PrimitiveId primitive;

    auto operator<=>(const SurfaceId &) const = default;
};

export struct TrianglePoint
{
    SurfaceId id;
    Barycentrics weights;
};

// World-space vectors corresponding to one unit along each local axis.
export struct Basis3
{
    Vec3 x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};
};

export class AffineTransform
{
  public:
    AffineTransform() = default;
    AffineTransform(Basis3 axes, Vec3 translation);

    [[nodiscard]] Vec3 to_world_point(Vec3 point) const noexcept
    {
        return translation_ + (axes_.x * point.x + axes_.y * point.y + axes_.z * point.z);
    }

    [[nodiscard]] Ray to_local_ray(const Ray &world) const noexcept
    {
        // Applying the inverse linear map without normalizing preserves world.at(t) for the same t.
        return {to_local_vector(world.origin - translation_), to_local_vector(world.direction)};
    }

  private:
    Basis3 axes_;
    Vec3 translation_{};
    Basis3 inverse_rows_;

    [[nodiscard]] Vec3 to_local_vector(Vec3 v) const noexcept
    {
        return {dot(inverse_rows_.x, v), dot(inverse_rows_.y, v), dot(inverse_rows_.z, v)};
    }
};

namespace
{
bool finite(Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

double axis_scale(Vec3 v)
{
    return std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)});
}
} // namespace

AffineTransform::AffineTransform(Basis3 axes, Vec3 translation) : axes_(axes), translation_(translation)
{
    if (!finite(translation) || !finite(axes.x) || !finite(axes.y) || !finite(axes.z))
    {
        throw std::invalid_argument("Instance transform must be finite");
    }
    // Scale the columns separately so their lengths cannot overflow or underflow the determinant.
    const Vec3 scales{axis_scale(axes.x), axis_scale(axes.y), axis_scale(axes.z)};
    if (scales.x == 0 || scales.y == 0 || scales.z == 0)
    {
        throw std::invalid_argument("Instance transform has a zero axis");
    }
    const Vec3 x = axes.x / scales.x, y = axes.y / scales.y, z = axes.z / scales.z;
    const Vec3 yz = cross(y, z);
    const double determinant = dot(x, yz);
    if (std::abs(determinant) <= 16 * std::numeric_limits<double>::epsilon())
    {
        throw std::invalid_argument("Instance axes are linearly dependent at working precision");
    }
    inverse_rows_ = {yz / determinant / scales.x, cross(z, x) / determinant / scales.y,
                     cross(x, y) / determinant / scales.z};
    if (!finite(inverse_rows_.x) || !finite(inverse_rows_.y) || !finite(inverse_rows_.z))
    {
        throw std::invalid_argument("Instance inverse is not representable");
    }
}

// Instances can share the same mesh. Its triangle storage must outlive use of this view.
export struct MeshInstance
{
    InstanceId id{};
    std::span<const Triangle> triangles;
    AffineTransform transform{};
};
} // namespace formlight
