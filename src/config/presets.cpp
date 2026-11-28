module formlight.scene_config;

import std;

namespace formlight
{
SceneConfig preset_config(ScenePreset preset)
{
    SceneConfig config;
    config.scene.preset = preset;
    if (preset == ScenePreset::ManyBalls)
    {
        config.camera = {.auto_frame = false,
                         .vertical_fov_degrees = 20,
                         .position = {13, 2, 3},
                         .target = {},
                         .lens = {.focus_distance = 10, .defocus_angle_degrees = 0.6}};
        config.render.mode = RenderMode::Path;
        config.render.extent = {256, 144};
        config.render.path_samples = 32;
        config.render.max_bounces = 8;
        config.render.sampling.seed = 7;
        config.scene.environment = {0.7, 0.8, 1};
    }
    else if (preset == ScenePreset::FinalScene)
    {
        config.camera = {.auto_frame = false,
                         .vertical_fov_degrees = 40,
                         .position = {478, 278, -600},
                         .target = {278, 278, 0},
                         .shutter = {0, 1}};
        const Sphere blue_boundary{{360, 150, 145}, 70};
        config.scene.spheres = {
            {.shape = {{400, 400, 200}, 50, {30, 0, 0}}, .albedo = {0.7, 0.3, 0.1}},
            {.shape = {{260, 150, 45}, 50}, .type = PathSurface::Dielectric},
            {.shape = {{0, 150, 145}, 50}, .albedo = {0.8, 0.8, 0.9}, .type = PathSurface::Metal, .fuzz = 1},
            {.shape = blue_boundary, .type = PathSurface::Dielectric},
            {.shape = {{400, 200, 400}, 100}, .albedo_texture = std::filesystem::path{FORMLIGHT_EARTH_TEXTURE}},
            {.shape = {{220, 280, 300}, 80}, .marble = MarbleSettings{0.2, 42}}};
        config.scene.media = {{blue_boundary, 0.2, {0.2, 0.4, 0.9}}, {Sphere{{}, 5000}, 0.0001, {1, 1, 1}}};
        config.render.mode = RenderMode::Path;
        config.render.extent = {192, 192};
        config.render.path_samples = 128;
        config.render.max_bounces = 16;
        config.render.bounce_sampling = BounceSampling::LightMixture;
        config.render.sampling.seed = 7;
    }
    return config;
}
} // namespace formlight
