import std;
import formlight.raster;
import formlight.ray_query;

auto main() -> int
{
    using namespace formlight;
    constexpr ImageExtent extent{64, 48};
    const PinholeCamera camera{
        {.position = {0, 0, 0}, .target = {0, 0, -1}, .up = {0, 1, 0}, .vertical_fov_radians = std::numbers::pi / 2},
        extent};
    constexpr Triangle triangle{
        .id = PrimitiveId{17},
        .vertices = {TriangleVertex{{-2, -1, -2}, {0, 0}}, TriangleVertex{{4, -2, -4}, {1, 0}},
                     TriangleVertex{{-1, 2, -3}, {0, 1}}},
    };
    std::array<ProjectedPoint, 3> projected{};
    std::ranges::transform(triangle.vertices, projected.begin(),
                           [&](const auto &vertex) { return camera.project(vertex.position).value(); });
    const RasterTriangle raster_triangle{triangle.id, projected};

    constexpr double tolerance = 1e-12;
    int covered = 0;
    double max_depth_error = 0, max_uv_error = 0;
    for (int row = 0; row < extent.height; ++row)
    {
        for (int column = 0; column < extent.width; ++column)
        {
            const auto point = pixel_center(column, row);
            const auto raster = raster_triangle.sample(point);
            const auto ray = camera.ray_for_sample(point);
            const auto hit = intersect(ray, triangle, {0, std::numeric_limits<double>::infinity()});
            if (raster && hit)
            {
                ++covered;
                const auto depth = camera.project(ray.at(hit->t)).value().view_depth;
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
                    return 1;
                }
                max_depth_error = std::max(max_depth_error, depth_error);
                max_uv_error = std::max({max_uv_error, u_error, v_error});
            }
            else if (raster || hit)
            {
                std::cerr << std::format("Coverage differs at ({}, {})\n", column, row);
                return 1;
            }
        }
    }

    // Projected corners are (8,36), (56,36), (24,8). No pixel center is on an edge.
    // At (32.5,24.5) the perspective weights are (290,251,368)/909.
    const auto reference = raster_triangle.sample(pixel_center(32, 24)).value();
    const auto uv = triangle.uv_at(reference.surface.weights);
    std::println("{} covered pixels, max depth error {:.3g}, max UV error {:.3g}", covered, max_depth_error,
                 max_uv_error);
    if (covered != 672 || !(std::abs(reference.view_depth - 896.0 / 303) <= tolerance &&
                            std::abs(uv.u - 251.0 / 909) <= tolerance && std::abs(uv.v - 368.0 / 909) <= tolerance))
    {
        std::cerr << "Triangle coverage or reference depth/UV differs from the analytic result\n";
        return 1;
    }
    return 0;
}
