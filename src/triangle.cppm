export module formlight.triangle;

import std;
export import formlight.geometry;

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
    TexCoord uv{};
    Vec3 normal{}; // Zero when the mesh has no vertex normals.
};

// Weights always refer to vertices 0, 1 and 2 of the original triangle.
struct Barycentrics
{
    double a, b, c;
};

struct Triangle
{
    PrimitiveId id{};
    std::array<TriangleVertex, 3> vertices{};
    std::size_t material_index{}; // Slot zero is the default material.

    [[nodiscard]] constexpr TexCoord uv_at(Barycentrics weights) const noexcept
    {
        const auto &[a, b, c] = vertices;
        return {
            .u = a.uv.u * weights.a + b.uv.u * weights.b + c.uv.u * weights.c,
            .v = a.uv.v * weights.a + b.uv.v * weights.b + c.uv.v * weights.c,
        };
    }

    // Normalize after the instance's inverse transpose has taken the result to world space.
    [[nodiscard]] constexpr Vec3 normal_at(Barycentrics weights) const noexcept
    {
        const auto &[a, b, c] = vertices;
        const Vec3 n = a.normal * weights.a + b.normal * weights.b + c.normal * weights.c;
        if (n.x == 0 && n.y == 0 && n.z == 0)
        {
            return cross(b.position - a.position, c.position - a.position);
        }
        return n;
    }
};

} // namespace formlight
