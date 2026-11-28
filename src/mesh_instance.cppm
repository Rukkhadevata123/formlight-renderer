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
    SurfaceId id{};
    Barycentrics weights{};
    WeightGradients gradients{}; // Rays without a footprint sample mip level zero.
};

// An empty filter accepts all geometric hits.
export using SurfaceFilter = std::function<bool(const TrianglePoint &)>;

export enum class CullFace
{
    None,
    Front,
    Back
};

// Classification uses the winding in mesh space, including for mirrored instances.
export using FaceCulling = std::function<CullFace(SurfaceId)>;

export constexpr bool culled(CullFace mode, bool back_face) noexcept
{
    return mode == (back_face ? CullFace::Back : CullFace::Front);
}

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

    [[nodiscard]] Vec3 to_world_vector(Vec3 v) const noexcept
    {
        return axes_.x * v.x + axes_.y * v.y + axes_.z * v.z;
    }

    [[nodiscard]] double handedness() const noexcept
    {
        return handedness_;
    }

    [[nodiscard]] Vec3 to_world_normal(Vec3 normal) const
    {
        const Vec3 world = inverse_rows_.x * normal.x + inverse_rows_.y * normal.y + inverse_rows_.z * normal.z;
        return unit_vector(world, "Surface normal must define a finite direction");
    }

    // Coordinate magnitudes before cancellation in interpolation and the local-to-world transform.
    [[nodiscard]] Vec3 world_point_magnitudes(Vec3 local_magnitudes) const noexcept
    {
        return abs_components(translation_) + abs_components(axes_.x) * local_magnitudes.x +
               abs_components(axes_.y) * local_magnitudes.y + abs_components(axes_.z) * local_magnitudes.z;
    }

    [[nodiscard]] Vec3 to_local_point(Vec3 world) const noexcept
    {
        return to_local_vector(world - translation_);
    }

    [[nodiscard]] Ray to_local_ray(const Ray &world) const noexcept
    {
        // Applying the inverse linear map without normalizing preserves world.at(t) for the same t.
        return {to_local_point(world.origin), to_local_vector(world.direction), world.time};
    }

    [[nodiscard]] Vec3 to_local_vector(Vec3 v) const noexcept
    {
        return {dot(inverse_rows_.x, v), dot(inverse_rows_.y, v), dot(inverse_rows_.z, v)};
    }

  private:
    Basis3 axes_;
    Vec3 translation_{};
    Basis3 inverse_rows_;
    double handedness_{1};
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
    handedness_ = std::copysign(1.0, determinant);
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
    bool sampling_target{};
};

export struct Bounds3
{
    Vec3 low, high;

    [[nodiscard]] static constexpr Bounds3 empty() noexcept
    {
        constexpr double inf = std::numeric_limits<double>::infinity();
        return {{inf, inf, inf}, {-inf, -inf, -inf}};
    }

    void include(Vec3 point) noexcept
    {
        low = {std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z)};
        high = {std::max(high.x, point.x), std::max(high.y, point.y), std::max(high.z, point.z)};
    }

    [[nodiscard]] Vec3 center() const noexcept
    {
        return {std::midpoint(low.x, high.x), std::midpoint(low.y, high.y), std::midpoint(low.z, high.z)};
    }

    [[nodiscard]] double radius() const noexcept
    {
        const Vec3 half = high - center();
        return std::hypot(half.x, half.y, half.z);
    }
};

export [[nodiscard]] Bounds3 world_bounds(std::span<const MeshInstance> instances)
{
    auto bounds = Bounds3::empty();
    for (const auto &instance : instances)
    {
        for (const auto &triangle : instance.triangles)
        {
            for (const auto &vertex : triangle.vertices)
            {
                bounds.include(instance.transform.to_world_point(vertex.position));
            }
        }
    }
    const double radius = bounds.radius();
    if (!(radius > 0) || !std::isfinite(radius))
    {
        throw std::invalid_argument("The scene has no finite spatial extent");
    }
    return bounds;
}
} // namespace formlight
