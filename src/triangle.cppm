export module formlight.triangle;

import std;
export import formlight.geometry;
export import formlight.image;

export namespace formlight
{
enum class PrimitiveId : std::uint32_t
{
};

struct TexCoord
{
    double u, v;
};

struct TriangleVertex
{
    Vec3 position{};
    std::array<TexCoord, 2> texcoords{};
    Vec3 normal{}; // Zero when the mesh has no vertex normals.
    LinearColor color{1, 1, 1, 1};
    Vec3 tangent{};
    double tangent_sign{1};
};

// Weights always refer to vertices 0, 1 and 2 of the original triangle.
struct Barycentrics
{
    double a, b, c;
};

// Only b and c are independent, since a + b + c = 1.
struct WeightDerivative
{
    double b{}, c{};
};

struct WeightGradients
{
    WeightDerivative dx{}, dy{};
};

struct UvGradients
{
    TexCoord dx{}, dy{};
};

struct Triangle
{
    PrimitiveId id{};
    std::array<TriangleVertex, 3> vertices{};
    std::size_t material_index{}; // Slot zero is the default material.

    [[nodiscard]] constexpr Vec3 position_at(Barycentrics weights) const noexcept
    {
        const auto &[a, b, c] = vertices;
        return a.position + (b.position - a.position) * weights.b + (c.position - a.position) * weights.c;
    }

    [[nodiscard]] constexpr TexCoord uv_at(Barycentrics weights, std::size_t set = 0) const
    {
        const auto a = vertices[0].texcoords.at(set);
        const auto b = vertices[1].texcoords.at(set);
        const auto c = vertices[2].texcoords.at(set);
        return {
            .u = a.u * weights.a + b.u * weights.b + c.u * weights.c,
            .v = a.v * weights.a + b.v * weights.b + c.v * weights.c,
        };
    }

    [[nodiscard]] constexpr Vec3 geometric_normal() const noexcept
    {
        const auto &[a, b, c] = vertices;
        return cross(b.position - a.position, c.position - a.position);
    }

    [[nodiscard]] constexpr UvGradients uv_gradients(WeightGradients weights, std::size_t set) const
    {
        const auto a = vertices[0].texcoords.at(set);
        const auto b = vertices[1].texcoords.at(set);
        const auto c = vertices[2].texcoords.at(set);
        const auto derivative = [&](WeightDerivative d) -> TexCoord
        { return {(b.u - a.u) * d.b + (c.u - a.u) * d.c, (b.v - a.v) * d.b + (c.v - a.v) * d.c}; };
        return {derivative(weights.dx), derivative(weights.dy)};
    }

    [[nodiscard]] constexpr LinearColor color_at(Barycentrics weights) const noexcept
    {
        const auto &a = vertices[0].color, &b = vertices[1].color, &c = vertices[2].color;
        const auto interpolate = [weights](double v0, double v1, double v2)
        { return static_cast<float>(v0 + (v1 - v0) * weights.b + (v2 - v0) * weights.c); };
        return {interpolate(a.r, b.r, c.r), interpolate(a.g, b.g, c.g), interpolate(a.b, b.b, c.b),
                interpolate(a.a, b.a, c.a)};
    }

    // Normalize after the instance's inverse transpose has taken the result to world space.
    [[nodiscard]] constexpr Vec3 normal_at(Barycentrics weights) const noexcept
    {
        const auto &[a, b, c] = vertices;
        const Vec3 n = a.normal * weights.a + b.normal * weights.b + c.normal * weights.c;
        if (n.x == 0 && n.y == 0 && n.z == 0)
        {
            return geometric_normal();
        }
        return n;
    }
};

} // namespace formlight
