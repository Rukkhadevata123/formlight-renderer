module formlight.raster;

import std;

namespace formlight
{
namespace
{
struct ClipVertex
{
    ViewPoint view;
    Barycentrics source_weights;
};

// Each depth plane can add at most one vertex to a convex polygon.
constexpr std::size_t max_vertices = 3 + 2;
using ClipPolygon = std::array<ClipVertex, max_vertices>;

enum class DepthPlane
{
    Near,
    Far
};

ClipVertex intersect_plane(const ClipVertex &lo, const ClipVertex &hi, double depth)
{
    const double t = (depth - lo.view.depth) / (hi.view.depth - lo.view.depth);
    const auto &a = lo.source_weights;
    const auto &b = hi.source_weights;
    return {
        .view = {std::lerp(lo.view.x, hi.view.x, t), std::lerp(lo.view.y, hi.view.y, t), depth},
        .source_weights = {std::lerp(a.a, b.a, t), std::lerp(a.b, b.b, t), std::lerp(a.c, b.c, t)},
    };
}

std::span<const ClipVertex> clip_plane(std::span<const ClipVertex> input, double depth, DepthPlane plane,
                                       ClipPolygon &output)
{
    const auto inside = [=](const ClipVertex &v)
    { return plane == DepthPlane::Near ? v.view.depth >= depth : v.view.depth <= depth; };
    if (std::ranges::all_of(input, inside))
    {
        return input;
    }

    std::size_t count = 0;
    const ClipVertex *previous = &input.back();
    for (const auto &current : input)
    {
        // Interpolate from the shallower endpoint for either traversal of a shared edge.
        const auto [lo, hi] = std::minmax(*previous, current, [](const ClipVertex &a, const ClipVertex &b)
                                          { return a.view.depth < b.view.depth; });
        if (lo.view.depth < depth && depth < hi.view.depth)
        {
            output[count++] = intersect_plane(lo, hi, depth);
        }
        if (inside(current))
        {
            output[count++] = current;
        }
        previous = &current;
    }
    return std::span{output}.first(count);
}

std::span<const RasterVertex> project_polygon(const PinholeCamera &camera, std::span<const ClipVertex> polygon,
                                              std::array<RasterVertex, max_vertices> &output)
{
    for (std::size_t i = 0; i < polygon.size(); ++i)
    {
        const auto point = camera.project_view(polygon[i].view);
        if (!point)
        {
            return {};
        }
        output[i] = {*point, polygon[i].source_weights};
    }
    return std::span{output}.first(polygon.size());
}
} // namespace

std::vector<RasterTriangle> RasterTriangle::prepare(const PinholeCamera &camera, std::span<const Triangle> triangles,
                                                    ViewDepthRange range)
{
    if (!(range.near_depth > 0.0 && range.near_depth < range.far_depth))
    {
        throw std::invalid_argument("Raster clipping requires 0 < near depth < far depth");
    }

    std::vector<RasterTriangle> result;
    result.reserve(triangles.size());
    for (const auto &triangle : triangles)
    {
        const auto &[a, b, c] = triangle.vertices;
        const std::array input{
            ClipVertex{camera.to_view(a.position), {1, 0, 0}},
            ClipVertex{camera.to_view(b.position), {0, 1, 0}},
            ClipVertex{camera.to_view(c.position), {0, 0, 1}},
        };
        ClipPolygon near_polygon{}, far_polygon{};
        auto polygon = clip_plane(input, range.near_depth, DepthPlane::Near, near_polygon);
        polygon = clip_plane(polygon, range.far_depth, DepthPlane::Far, far_polygon);

        std::array<RasterVertex, max_vertices> screen_polygon{};
        const auto projected = project_polygon(camera, polygon, screen_polygon);
        for (std::size_t i = 2; i < projected.size(); ++i)
        {
            result.push_back(RasterTriangle{triangle.id, projected[0], projected[i - 1], projected[i]});
        }
    }
    return result;
}
} // namespace formlight
