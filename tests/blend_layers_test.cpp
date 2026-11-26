import std;
import formlight.preview;

using namespace formlight;

int main(int argc, char **argv)
try
{
    if (argc != 2)
    {
        return 1;
    }
    const auto scene = GltfScene::load_glb(argv[1], GltfContent::BaseColor);
    if (!scene)
    {
        throw std::runtime_error(scene.error());
    }
    const PinholeCamera camera{{{0, 1.3, 4}, {0, 1.3, 0}, {0, 1, 0}, std::numbers::pi / 3}, {64, 64}};
    const auto view = clip_to_bounds(camera, world_bounds(scene->instances()));
    const Vec3 coral{1, 0.12, 0.05}, blue{0.04, 0.34, 1}, stone{0.76, 0.70, 0.54};
    // Both sheets tilt by the same angle from the frontal directional light.
    const double cosine = 1.0 / std::sqrt(1.0 + 0.4 * 0.4);
    for (const auto shadows : {ShadowMethod::Map, ShadowMethod::Ray})
    {
        const PreviewRenderer renderer{*scene, DirectionalLighting{DirectionalLight{{0, 0, 1}}, shadows}};
        const auto frame = renderer.render(view);
        const auto clear = frame[0, 0];
        const Vec3 background{clear.r, clear.g, clear.b};
        const auto check = [&](Vec3 world, InstanceId front, Vec3 expected)
        {
            const auto p = camera.project(world).value().image;
            const PixelCoord pixel{static_cast<int>(p.x), static_cast<int>(p.y)};
            const auto color = frame[pixel.y, pixel.x];
            const auto report = renderer.diagnose(view, pixel);
            const auto delta = abs_components(Vec3{color.r, color.g, color.b} - expected);
            if (std::max({delta.x, delta.y, delta.z}) > 1e-6 || color.a != 1 || !report.surface ||
                report.surface->hit.surface.id.instance != front || report.samples.size() != 1 ||
                std::tie(color.r, color.g, color.b) != std::tie(report.color.r, report.color.g, report.color.b))
            {
                throw std::runtime_error(std::format("Incorrect blend or pick at pixel ({}, {})", pixel.x, pixel.y));
            }
        };
        check({-0.7, 1.6, 0}, InstanceId{0}, (coral * 0.5 + blue * 0.25) * cosine + background * 0.25);
        check({0.7, 1.6, 0}, InstanceId{1}, (blue * 0.5 + coral * 0.25) * cosine + background * 0.25);
        // The block occludes one or both sheets. Transparent sheets cast no shadow on it.
        check({0.7, 0.72, 0.15}, InstanceId{1}, blue * (0.5 * cosine) + stone * 0.5);
        check({0, 0.72, 0.15}, InstanceId{2}, stone);
    }
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
