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
    Vec3 position;
    TexCoord uv;
};

// Weights always refer to vertices 0, 1 and 2 of the original triangle.
struct Barycentrics
{
    double a, b, c;
};

struct Triangle
{
    PrimitiveId id;
    std::array<TriangleVertex, 3> vertices;

    [[nodiscard]] constexpr TexCoord uv_at(Barycentrics weights) const noexcept
    {
        const auto &[a, b, c] = vertices;
        return {
            .u = a.uv.u * weights.a + b.uv.u * weights.b + c.uv.u * weights.c,
            .v = a.uv.v * weights.a + b.uv.v * weights.b + c.uv.v * weights.c,
        };
    }
};

} // namespace formlight
