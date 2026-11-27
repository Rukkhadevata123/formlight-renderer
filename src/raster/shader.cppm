export module formlight.shader;

import std;
export import formlight.raster;

namespace formlight
{
export template <class Varying> struct VertexOutput
{
    Vec3 position; // Mesh-local, before the instance and camera transforms.
    Varying varying;
};

export template <class S>
concept RasterShader = requires(const S &shader, const TriangleVertex &vertex, const typename S::Varying &v, double w) {
    { shader.vertex(vertex) } -> std::same_as<VertexOutput<typename S::Varying>>;
    { v * w + v * w + v * w } -> std::convertible_to<typename S::Varying>;
    { shader.fragment(v) } -> std::same_as<std::optional<LinearColor>>;
};

// Single-sample color and depth pass. An empty fragment output discards both writes.
export template <RasterShader Shader>
[[nodiscard]] ColorImage render_shader(const PinholeCamera &camera, std::span<const MeshInstance> instances,
                                       ViewDepthRange range, const Shader &shader, LinearColor clear = {},
                                       std::stop_token stop = {})
{
    const auto extent = camera.extent();
    ColorImage image{extent, clear};
    std::vector<std::pair<double, SurfaceId>> depth(static_cast<std::size_t>(extent.width) * extent.height,
                                                    {std::numeric_limits<double>::infinity(), {}});
    const auto transform = [&](const Triangle &triangle)
    {
        const auto &[a, b, c] = triangle.vertices;
        return std::array{shader.vertex(a), shader.vertex(b), shader.vertex(c)};
    };
    const auto draw = [&](const RasterTriangle &piece, const auto &vertices)
    {
        const auto shade = [&](int x, int y, const RasterHit &hit)
        {
            auto &nearest = depth[static_cast<std::size_t>(y) * extent.width + x];
            const auto key = hit.depth_key();
            if (key >= nearest)
            {
                return;
            }
            // These are perspective weights on the original vertices, including after a clipping split.
            const auto [a, b, c] = hit.surface.weights;
            const typename Shader::Varying varying =
                vertices[0].varying * a + vertices[1].varying * b + vertices[2].varying * c;
            if (const auto color = shader.fragment(varying))
            {
                image[y, x] = *color;
                nearest = key;
            }
        };
        piece.for_each_hit(extent, shade);
    };
    RasterTriangle::for_each_clipped(camera, instances, range, transform, draw, stop);
    return image;
}
} // namespace formlight
