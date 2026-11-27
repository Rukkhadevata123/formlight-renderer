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

using ClipPolygon = std::array<ClipVertex, max_clipped_vertices>;

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

} // namespace

RasterTriangle::ProjectedPolygon RasterTriangle::clip_and_project(const PinholeCamera &camera, ViewDepthRange range,
                                                                  const std::array<ViewPoint, 3> &points)
{
    const std::array input{ClipVertex{points[0], {1, 0, 0}}, ClipVertex{points[1], {0, 1, 0}},
                           ClipVertex{points[2], {0, 0, 1}}};
    ClipPolygon near_polygon{}, far_polygon{};
    auto polygon = clip_plane(input, range.near_depth, DepthPlane::Near, near_polygon);
    polygon = clip_plane(polygon, range.far_depth, DepthPlane::Far, far_polygon);

    ProjectedPolygon projected;
    for (const auto &vertex : polygon)
    {
        const auto point = camera.project_view(vertex.view);
        if (!point)
        {
            return {};
        }
        projected.vertices[projected.size++] = {*point, vertex.source_weights};
    }
    return projected;
}

std::vector<RasterTriangle> RasterTriangle::prepare(const PinholeCamera &camera,
                                                    std::span<const MeshInstance> instances, ViewDepthRange range,
                                                    std::stop_token stop)
{
    std::vector<RasterTriangle> result;
    std::size_t triangle_count = 0;
    for (const auto &instance : instances)
    {
        triangle_count += instance.triangles.size();
    }
    result.reserve(triangle_count);
    for_each_clipped(
        camera, instances, range, [](const Triangle &triangle) -> const auto & { return triangle.vertices; },
        [&](RasterTriangle piece, const auto &) { result.push_back(std::move(piece)); }, stop);
    return result;
}
} // namespace formlight
