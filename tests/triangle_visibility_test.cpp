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

bool compare_triangle(const PinholeCamera &camera, const MeshInstance &instance, ViewDepthRange range,
                      int expected_coverage)
{
    const auto scene = std::span{&instance, 1};
    const auto &triangle = instance.triangles.front();
    const SurfaceId expected{instance.id, triangle.id};
    const auto pieces = RasterTriangle::prepare(camera, scene, range);
    int covered = 0;
    double max_depth_error = 0, max_uv_error = 0;
    for (int row = 0; row < extent.height; ++row)
    {
        for (int column = 0; column < extent.width; ++column)
        {
            const auto point = pixel_center(column, row);
            const auto raster = sample_pieces(pieces, point);
            const auto ray = camera.ray_for_sample(point);
            const auto hit = intersect(ray, scene, {0, std::numeric_limits<double>::infinity()});
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
                if (raster->surface.id != expected || hit->surface.id != expected ||
                    !(depth_error <= tolerance && u_error <= tolerance && v_error <= tolerance))
                {
                    std::cerr << std::format("Hit differs at ({}, {}): raster instance/primitive/depth/UV ({}, {}, {}, "
                                             "{}, {}), ray ({}, {}, {}, {}, {})\n",
                                             column, row, std::to_underlying(raster->surface.id.instance),
                                             std::to_underlying(raster->surface.id.primitive), raster->view_depth,
                                             raster_uv.u, raster_uv.v, std::to_underlying(hit->surface.id.instance),
                                             std::to_underlying(hit->surface.id.primitive), depth, ray_uv.u, ray_uv.v);
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

bool check_ground_and_panel()
{
    const PinholeCamera camera{
        {.position = {0, 3, 0}, .target = {0, 0, 0}, .up = {0, 0, -1}, .vertical_fov_radians = std::numbers::pi / 2},
        extent};
    constexpr TriangleVertex nw{{-2, 0, -2}, {0, 0}}, ne{{2, 0, -2}, {1, 0}}, se{{2, 0, 2}, {1, 1}},
        sw{{-2, 0, 2}, {0, 1}};
    constexpr PrimitiveId upper{31}, lower{7};
    constexpr InstanceId floor{2}, right{8}, left{4};
    std::array ground_mesh{Triangle{upper, {nw, ne, se}}, Triangle{lower, {nw, se, sw}}};
    // Both panels use the same local triangle. Its primitive ID also occurs in the ground mesh.
    constexpr std::array panel_mesh{
        Triangle{upper,
                 {TriangleVertex{{-0.5, 0.75, -0.75}, {0, 0}}, TriangleVertex{{2, 0.5, -1}, {1, 0}},
                  TriangleVertex{{2.5, 0.25, 0}, {0, 1}}}}};
    std::array scene{
        MeshInstance{floor, ground_mesh},
        MeshInstance{right, panel_mesh, {Basis3{.x = {0.5, 0, 0}, .y = {0, 2, 0}}, {}}},
        MeshInstance{
            left, panel_mesh, {Basis3{.x = {-0.375, 0, 0}, .y = {0, 1.5, 0}, .z = {0, 0, -0.75}}, {0, 0.75, 0}}},
    };
    for (int pass = 0; pass < 2; ++pass)
    {
        const auto pieces = RasterTriangle::prepare(camera, scene, {1, 5});
        std::vector<std::optional<RasterHit>> pixels(extent.width * extent.height);
        const VisibilityBuffer buffer{pixels.data(), extent.height, extent.width};
        for (const auto &piece : pieces)
        {
            piece.rasterize(buffer);
        }
        const auto covered = std::ranges::count_if(pixels, &std::optional<RasterHit>::has_value);
        if (covered != 32 * 32)
        {
            std::cerr << std::format("Ground covers {} pixels, expected 1024\n", covered);
            return false;
        }

        // The right panel projects to (28,12), (44,12), (44,24); the left is rotated 180 degrees about image center.
        const auto on_right = [](ImagePoint p) { return p.x < 44 && p.y > 12 && 4 * (p.y - 12) < 3 * (p.x - 28); };
        int right_pixels = 0, left_pixels = 0;
        for (int row = 8; row < 40; ++row)
        {
            for (int column = 16; column < 48; ++column)
            {
                const auto point = pixel_center(column, row);
                const bool on_seam = row == column - 8;
                SurfaceId expected{floor, row <= column - 8 ? upper : lower};
                if (on_right(point))
                {
                    expected = {right, panel_mesh.front().id};
                    ++right_pixels;
                }
                else if (on_right({extent.width - point.x, extent.height - point.y}))
                {
                    expected = {left, panel_mesh.front().id};
                    ++left_pixels;
                }
                // Check raw coverage, since a depth test could hide two owners of the same edge.
                if (on_seam && !sample_pieces(pieces, point))
                {
                    std::cerr << std::format("Gap on the shared edge at ({}, {})\n", column, row);
                    return false;
                }
                const auto &pixel = buffer[row, column];
                const auto ray = camera.ray_for_sample(point);
                const auto hit = intersect(ray, scene, {0.0, std::numeric_limits<double>::infinity()});
                if (!pixel || pixel->surface.id != expected || !hit ||
                    !(hit->surface.id == expected || (on_seam && hit->surface.id == SurfaceId{floor, lower})) ||
                    !(std::abs(pixel->view_depth - camera.to_view(ray.at(hit->t)).depth) <= tolerance))
                {
                    std::cerr << std::format("Wrong visible surface at ({}, {}) in draw order {}\n", column, row, pass);
                    return false;
                }
                if (expected.instance != floor)
                {
                    const auto uv = panel_mesh.front().uv_at(pixel->surface.weights);
                    const auto ray_uv = panel_mesh.front().uv_at(hit->surface.weights);
                    if (!(std::abs(uv.u - ray_uv.u) <= tolerance && std::abs(uv.v - ray_uv.v) <= tolerance))
                    {
                        std::cerr << std::format("Panel UV differs at ({}, {})\n", column, row);
                        return false;
                    }
                }
            }
        }
        const auto ray = camera.ray_for_sample(pixel_center(40, 15));
        const RayInterval behind_panel{2.75 / -ray.direction.y, std::numeric_limits<double>::infinity()};
        const auto ground = intersect(ray, scene, behind_panel);
        if (!ground || ground->surface.id != SurfaceId{floor, upper})
        {
            std::cerr << "The ray interval behind the panel must still hit the ground\n";
            return false;
        }
        std::println("Draw order {}: {} visible pixels, right/left panel {}/{}, 32 shared-edge samples", pass, covered,
                     right_pixels, left_pixels);
        std::ranges::reverse(scene);
        std::ranges::reverse(ground_mesh);
    }
    return true;
}
} // namespace

auto main() -> int
{
    const PinholeCamera camera{
        {.position = {0, 0, 0}, .target = {0, 0, -1}, .up = {0, 1, 0}, .vertical_fov_radians = std::numbers::pi / 2},
        extent};
    constexpr Triangle triangle{
        .id = PrimitiveId{17},
        .vertices = {TriangleVertex{{-2, 0, -2}, {0, 0}}, TriangleVertex{{2, -2, 2}, {1, 0}},
                     TriangleVertex{{-1, 6, -2}, {0, 1}}},
    };
    // A 45-degree rotation and unequal axis lengths give world corners (-2,-1,-2), (4,-2,-4), (-1,2,-3).
    const MeshInstance tilted{InstanceId{1},
                              std::span{&triangle, 1},
                              {Basis3{.x = {1, 0, -1}, .y = {0, 0.5, 0}, .z = {0.5, 0, 0.5}}, {1, -1, -3}}};
    constexpr ViewDepthRange full{1, 5};

    // At (32.5,24.5) the original triangle's perspective weights are (290,251,368)/909.
    const auto pieces = RasterTriangle::prepare(camera, std::span{&tilted, 1}, full);
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
    const MeshInstance crossing{InstanceId{2}, std::span{&crosses_eye, 1}};
    const bool passed = compare_triangle(camera, tilted, full, 672) && compare_triangle(camera, crossing, full, 600) &&
                        compare_triangle(camera, tilted, {2.5, 3.5}, 471) &&
                        compare_triangle(camera, tilted, {5, 6}, 0) && check_ground_and_panel();
    return passed ? 0 : 1;
}
