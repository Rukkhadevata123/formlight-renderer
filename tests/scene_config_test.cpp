#include <oneapi/tbb/task_arena.h>

import std;
import formlight.scene_toml;
import formlight.preview;

namespace
{
void round_trip(const std::filesystem::path &source, const std::filesystem::path &destination,
                formlight::LightKind kind)
{
    using namespace formlight;
    auto config = load_scene_config(source);
    config.render.mode = RenderMode::Hybrid;
    config.render.extent = {48, 32};
    config.render.ssaa = 2;
    config.render.path_samples = 11;
    config.render.max_bounces = 3;
    config.render.bounce_sampling = BounceSampling::LightMixture;
    config.scene.path_materials = {{0, PathSurface::Dielectric, 1.33}};
    const auto geometry = GltfScene::load(config.scene.asset, GltfContent::Geometry).value();
    config.scene.sampling_targets = {std::to_underlying(geometry.instances().front().id)};
    config.render.sampling = {4, std::numeric_limits<std::uint64_t>::max()};
    config.camera.auto_frame = false;
    config.camera.position = {2, 1, 3};
    config.camera.target = {0, -0.2, 0};
    auto &light = config.lighting.value().lights.at(0);
    light.width = 0.75;
    config.lighting->ambient = {0.2, 0.1, 0.4};
    light.type = kind;
    if (kind == LightKind::Point)
    {
        light.position = {-1, 2, 1};
        light.attenuation = {0.8, 0.25, 0.1};
        light.color = {1, 0.2, 0.4};
        light.intensity = 6;
    }

    config.lighting->lights.push_back({.type = LightKind::Directional, .enabled = false});
    config.lighting->lights.push_back({.type = LightKind::Directional, .pcf = true, .color = {0.1, 0.6, 1}});

    // Change worker counts across the round trip; fixed samples must give identical linear pixels.
    const auto before = oneapi::tbb::task_arena{1}.execute([&] { return render_scene(prepare_render(config)); });
    config.display = {.exposure_ev = -2};
    constexpr LinearColor highlights{8, 16, 2};
    if (DisplayTransform{config.display}.rgb8(highlights) != std::array<std::uint8_t, 3>{255, 255, 188})
    {
        throw std::runtime_error("The default display transform must clip highlights after exposure");
    }

    config.display.tone_mapping = ToneMapping::Reinhard;
    save_scene_config(destination, config);
    const auto loaded = load_scene_config(destination);
    if (loaded.scene.sampling_targets != config.scene.sampling_targets)
    {
        throw std::runtime_error("Save As lost the selected sampling target node");
    }
    const auto &override = loaded.scene.path_materials.at(0);
    if (override.material != 0 || override.type != PathSurface::Dielectric || override.ior != 1.33)
    {
        throw std::runtime_error("Save As lost the dielectric material override");
    }
    if (!loaded.lighting.value().lights.back().pcf)
    {
        throw std::runtime_error("Save As lost the directional light's PCF setting");
    }
    // Exposed values 2, 4 and 0.5 map to 2/3, 4/5 and 1/3 before sRGB encoding.
    if (DisplayTransform{loaded.display}.rgb8(highlights) != std::array<std::uint8_t, 3>{213, 231, 156})
    {
        throw std::runtime_error("Saved exposure and Reinhard mapping did not reproduce the expected sRGB bytes");
    }
    if (!std::filesystem::equivalent(config.scene.asset, loaded.scene.asset) ||
        loaded.render.sampling.seed != config.render.sampling.seed || loaded.render.ssaa != 2 ||
        loaded.render.path_samples != 11 || loaded.render.max_bounces != 3 ||
        loaded.render.bounce_sampling != BounceSampling::LightMixture || loaded.camera.auto_frame)
    {
        throw std::runtime_error("Save As lost the asset reference, camera, or full-width seed");
    }
    const auto after = oneapi::tbb::task_arena{4}.execute([&] { return render_scene(prepare_render(loaded)); });
    if (after.extent().width != before.extent().width || after.extent().height != before.extent().height)
    {
        throw std::runtime_error("Image extent changed after Save As");
    }
    for (int y = 0; y < before.extent().height; ++y)
    {
        for (int x = 0; x < before.extent().width; ++x)
        {
            const auto a = before[y, x], b = after[y, x];
            if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a)
            {
                throw std::runtime_error("Render from the edited config differs from its TOML round trip");
            }
        }
    }
}
void check_sphere_scene(const std::filesystem::path &destination)
{
    using namespace formlight;
    SceneConfig config;
    config.render.mode = RenderMode::Path;
    config.render.extent = {1, 1};
    config.scene.spheres = {
        {.shape = {{2, 0, 0}, 0.75}, .type = PathSurface::Dielectric, .ior = 1.33, .sampling_target = true}};
    save_scene_config(destination, config);
    const auto loaded = load_scene_config(destination);
    const auto &sphere = loaded.scene.spheres.at(0);
    if (!loaded.scene.asset.empty() || sphere.shape.center != Vec3{2, 0, 0} || sphere.shape.radius != 0.75 ||
        sphere.type != PathSurface::Dielectric || sphere.ior != 1.33 || !sphere.sampling_target)
    {
        throw std::runtime_error("Sphere-only scene changed during TOML round trip");
    }
    const auto report = diagnose_pixel(prepare_render(loaded), {0, 0});
    const auto &point = report.samples.at(0).surface.value().hit.surface;
    if (std::get<SphereCoordinates>(point.coordinates).index != 0)
    {
        throw std::runtime_error("Automatic framing missed the sphere-only scene");
    }
}
} // namespace

auto main(int argc, char **argv) -> int
{
    if (argc != 2)
    {
        return 1;
    }
    const auto scratch = std::filesystem::current_path() / std::format("config-test-{:08x}", std::random_device{}());
    if (!std::filesystem::create_directory(scratch))
    {
        return 1;
    }
    const auto saved = scratch / "saved.toml";
    int status = 0;
    try
    {
        round_trip(argv[1], saved, formlight::LightKind::Rectangle);
        round_trip(argv[1], saved, formlight::LightKind::Point);
        check_sphere_scene(saved);
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        status = 1;
    }
    std::filesystem::remove(saved);
    std::filesystem::remove(scratch);
    return status;
}
