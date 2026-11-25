import std;
import formlight.gltf;
import formlight.preview;
import formlight.raster;
import formlight.ray_query;

namespace
{
bool check_diffuse_box(const formlight::GltfScene &scene)
{
    using namespace formlight;
    const auto view = frame_scene(scene, {64, 48});
    const auto base = render_preview(scene, view);
    const auto lit = render_preview(scene, view, PreviewLighting{DirectionalLight{{-2, 2, 1}}});
    const auto check_face = [&](Vec3 center, float cosine)
    {
        const auto point = view.camera.project(center).value().image;
        const int x = static_cast<int>(point.x), y = static_cast<int>(point.y);
        const auto color = base[y, x], shaded = lit[y, x];
        return std::max({color.r, color.g, color.b}) > 0.01f && std::abs(shaded.r - color.r * cosine) < 1e-6f &&
               std::abs(shaded.g - color.g * cosine) < 1e-6f && std::abs(shaded.b - color.b * cosine) < 1e-6f;
    };
    // The three visible face normals in world space are +X, +Y and +Z.
    return check_face({0.5, 0, 0}, 0) && check_face({0, 0.5, 0}, 2.0f / 3) && check_face({0, 0, 0.5}, 1.0f / 3);
}
bool check_ground_shadow(formlight::GltfScene &scene)
{
    using namespace formlight;
    add_preview_ground(scene, world_bounds(scene.instances()));
    const PinholeCamera overhead{
        {.position = {0, 4, 0}, .target = {}, .up = {0, 0, -1}, .vertical_fov_radians = std::numbers::pi / 3},
        {128, 128}};
    const PreviewView view{overhead, {1, 6}};
    const auto lit = render_preview(scene, view, PreviewLighting{});
    const auto shadowed = render_preview(scene, view, PreviewLighting{.shadows = ShadowMethod::Map});
    const auto check = [&](Vec3 world, bool blocked)
    {
        const auto p = overhead.project(world).value().image;
        const int x = static_cast<int>(p.x), y = static_cast<int>(p.y);
        const auto plain = lit[y, x], mapped = shadowed[y, x];
        return plain.r > 0 && (blocked ? mapped.r == 0 && mapped.g == 0 && mapped.b == 0
                                       : mapped.r == plain.r && mapped.g == plain.g && mapped.b == plain.b);
    };
    // At floor height the top of the unit box projects by (-1/4, 0, -3/4).
    return check({0, -0.5, -0.8}, true) && check({0, -0.5, -1.45}, false) && check({0, 0.5, 0}, false);
}
} // namespace

auto main(int argc, char **argv) -> int
{
    using namespace formlight;
    if (argc != 2)
    {
        std::cerr << "Expected the BoxTextured.glb fixture path\n";
        return 1;
    }
    auto scene = GltfScene::load_glb(argv[1], GltfContent::BaseColor);
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
    if (instances.front().triangles[6].vertices.front().normal.y != -1)
    {
        std::cerr << "Expected the authored local -Y normal on the box front face\n";
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
    const auto image = render_preview(*scene, {camera, {1, 4}});
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
    // The top and bottom of the same rotated face sample the blue sky and green ground in the embedded PNG.
    const auto matches_rgb = [&](int x, int y, std::array<int, 3> expected)
    {
        const auto color = image[y, x];
        return std::lround(linear_to_srgb(color.r) * 255) == expected[0] &&
               std::lround(linear_to_srgb(color.g) * 255) == expected[1] &&
               std::lround(linear_to_srgb(color.b) * 255) == expected[2];
    };
    if (!matches_rgb(32, 19, {108, 173, 223}) || !matches_rgb(32, 27, {92, 135, 39}))
    {
        std::cerr << "Repeated base-color texture has the wrong colors or orientation\n";
        return 1;
    }
    if (!check_diffuse_box(*scene))
    {
        std::cerr << "Directional light disagrees with the world-space box face normals\n";
        return 1;
    }
    // On a repeat boundary, linear filtering blends the last and first texel.
    ColorImage seam{{2, 1}};
    seam[0, 1] = {1, 1, 1, 1};
    if (sample_texture(seam, {-1, 0.5}, {}).r != 0.5f)
    {
        std::cerr << "Bilinear repeat failed across the negative U boundary\n";
        return 1;
    }
    if (!check_ground_shadow(*scene))
    {
        std::cerr << "Shadow map disagrees with the box footprint or shadows a lit surface\n";
        return 1;
    }
    return 0;
}
