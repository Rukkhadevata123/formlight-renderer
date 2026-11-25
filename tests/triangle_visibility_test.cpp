import std;
import formlight.raster;
import formlight.ray_query;

namespace
{
using namespace formlight;
constexpr ImageExtent extent{64, 48};
constexpr double tolerance = 1e-12;

std::optional<RasterHit> sample_pieces(std::span<const RasterTriangle> pieces, ImagePoint point)
{
    std::optional<RasterHit> hit;
    for (const auto &piece : pieces)
    {
        if (const auto sample = piece.sample(point))
        {
            if (hit)
            {
                throw std::runtime_error(std::format("Two pieces own sample ({}, {})", point.x, point.y));
            }
            hit = sample;
        }
    }
    return hit;
}

bool compare(const PinholeCamera &camera, const Triangle &triangle, ViewDepthRange range, int expected_coverage)
{
    const auto pieces = RasterTriangle::prepare(camera, std::span{&triangle, 1}, range);
    int covered = 0;
    double max_depth_error = 0, max_uv_error = 0;
    for (int row = 0; row < extent.height; ++row)
    {
        for (int column = 0; column < extent.width; ++column)
        {
            const auto point = pixel_center(column, row);
            const auto raster = sample_pieces(pieces, point);
            const auto ray = camera.ray_for_sample(point);
            const auto hit = intersect(ray, triangle, {0, std::numeric_limits<double>::infinity()});
            const double depth = hit ? camera.to_view(ray.at(hit->t)).depth : 0.0;
            const bool ray_visible = hit && range.contains(depth);
            if (raster && ray_visible)
            {
                ++covered;
                const auto raster_uv = triangle.uv_at(raster->surface.weights);
                const auto ray_uv = triangle.uv_at(hit->surface.weights);
                const double depth_error = std::abs(raster->view_depth - depth);
                const double u_error = std::abs(raster_uv.u - ray_uv.u);
                const double v_error = std::abs(raster_uv.v - ray_uv.v);
                if (raster->surface.primitive != triangle.id || hit->surface.primitive != triangle.id ||
                    !(depth_error <= tolerance && u_error <= tolerance && v_error <= tolerance))
                {
                    std::cerr << std::format(
                        "Hit differs at ({}, {}): raster ID/depth/UV ({}, {}, {}, {}), ray ({}, {}, {}, {})\n", column,
                        row, std::to_underlying(raster->surface.primitive), raster->view_depth, raster_uv.u,
                        raster_uv.v, std::to_underlying(hit->surface.primitive), depth, ray_uv.u, ray_uv.v);
                    return false;
                }
                max_depth_error = std::max(max_depth_error, depth_error);
                max_uv_error = std::max({max_uv_error, u_error, v_error});
            }
            else if (raster || ray_visible)
            {
                std::cerr << std::format("Coverage differs at ({}, {})\n", column, row);
                return false;
            }
        }
    }

    std::println("ID {}, depth [{}, {}]: {} pieces, {} covered pixels (expected {}), max depth/UV error {:.3g}/{:.3g}",
                 std::to_underlying(triangle.id), range.near_depth, range.far_depth, pieces.size(), covered,
                 expected_coverage, max_depth_error, max_uv_error);
    return covered == expected_coverage;
}
} // namespace

auto main() -> int
{
    const PinholeCamera camera{
        {.position = {0, 0, 0}, .target = {0, 0, -1}, .up = {0, 1, 0}, .vertical_fov_radians = std::numbers::pi / 2},
        extent};
    constexpr Triangle triangle{
        .id = PrimitiveId{17},
        .vertices = {TriangleVertex{{-2, -1, -2}, {0, 0}}, TriangleVertex{{4, -2, -4}, {1, 0}},
                     TriangleVertex{{-1, 2, -3}, {0, 1}}},
    };
    constexpr ViewDepthRange full{1, 5};

    // At (32.5,24.5) the original triangle's perspective weights are (290,251,368)/909.
    const auto pieces = RasterTriangle::prepare(camera, std::span{&triangle, 1}, full);
    const auto reference = sample_pieces(pieces, pixel_center(32, 24)).value();
    const auto uv = triangle.uv_at(reference.surface.weights);
    if (!(std::abs(reference.view_depth - 896.0 / 303) <= tolerance && std::abs(uv.u - 251.0 / 909) <= tolerance &&
          std::abs(uv.v - 368.0 / 909) <= tolerance))
    {
        std::cerr << "Reference depth/UV differs from the analytic result\n";
        return 1;
    }

    // One vertex is behind the eye. The near cut projects to a trapezoid with a shared fan diagonal on x + y = 56.
    constexpr Triangle crosses_eye{
        .id = PrimitiveId{23},
        .vertices = {TriangleVertex{{0, 2.5, 1}, {1, 1}}, TriangleVertex{{-1.5, -1.5, -3}, {1, 0}},
                     TriangleVertex{{1, -1.5, -3}, {0, 1}}},
    };
    const bool passed = compare(camera, triangle, full, 672) && compare(camera, crosses_eye, full, 600) &&
                        compare(camera, triangle, {2.5, 3.5}, 471) && compare(camera, triangle, {5, 6}, 0);
    return passed ? 0 : 1;
}
