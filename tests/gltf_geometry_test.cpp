import std;
import formlight.gltf;
import formlight.raster;
import formlight.ray_query;

auto main(int argc, char **argv) -> int
{
    using namespace formlight;
    if (argc != 2)
    {
        std::cerr << "Expected the BoxTextured.glb fixture path\n";
        return 1;
    }
    const auto scene = GltfGeometry::load_glb(argv[1]);
    if (!scene)
    {
        std::cerr << scene.error() << '\n';
        return 1;
    }
    const auto instances = scene->instances();
    if (scene->mesh_count() != 1 || instances.size() != 1 || instances.front().triangles.size() != 12)
    {
        std::cerr << "Expected one box mesh, one instance and twelve triangles\n";
        return 1;
    }
    constexpr ImageExtent extent{64, 48};
    constexpr double camera_x = 1.0 / 48;
    const PinholeCamera camera{{.position = {camera_x, 0, 2.5},
                                .target = {camera_x, 0, 0},
                                .up = {0, 1, 0},
                                .vertical_fov_radians = std::numbers::pi / 2},
                               extent};
    std::vector<std::optional<RasterHit>> pixels(extent.width * extent.height);
    const VisibilityBuffer buffer{pixels.data(), extent.height, extent.width};
    for (const auto &triangle : RasterTriangle::prepare(camera, instances, {1, 4}))
    {
        triangle.rasterize(buffer);
    }
    for (int y = 0; y < extent.height; ++y)
    {
        for (int x = 0; x < extent.width; ++x)
        {
            const auto point = pixel_center(x, y);
            const auto ray = camera.ray_for_sample(point);
            const auto hit = intersect(ray, instances, {0, std::numeric_limits<double>::infinity()});
            const auto &pixel = buffer[y, x];
            const bool visible = x >= 26 && x < 38 && y >= 18 && y < 30;
            if (pixel.has_value() != visible || hit.has_value() != visible)
            {
                std::cerr << std::format("Box silhouette differs at ({}, {})\n", x, y);
                return 1;
            }
            if (visible)
            {
                // The parent's rotation puts local -Y at world +Z. The face uses U in [3,4].
                const auto id = std::to_underlying(pixel->surface.id.primitive);
                if (pixel->surface.id != hit->surface.id || pixel->surface.id.instance != InstanceId{1} ||
                    (id != 6 && id != 7))
                {
                    std::cerr << std::format("Wrong front face at ({}, {})\n", x, y);
                    return 1;
                }
                const auto &face = instances.front().triangles[id];
                const auto raster_uv = face.uv_at(pixel->surface.weights);
                const auto ray_uv = face.uv_at(hit->surface.weights);
                const double u = 3.5 - (camera_x + (point.x - 32) / 12);
                const double v = 0.5 - (24 - point.y) / 12;
                constexpr double tolerance = 1e-12;
                if (!(std::abs(pixel->view_depth - 2) <= tolerance &&
                      std::abs(camera.to_view(ray.at(hit->t)).depth - 2) <= tolerance &&
                      std::abs(raster_uv.u - u) <= tolerance && std::abs(raster_uv.v - v) <= tolerance &&
                      std::abs(ray_uv.u - u) <= tolerance && std::abs(ray_uv.v - v) <= tolerance))
                {
                    std::cerr << std::format("Box depth or UV differs at ({}, {})\n", x, y);
                    return 1;
                }
            }
        }
    }
    return 0;
}
