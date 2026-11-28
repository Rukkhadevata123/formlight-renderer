module formlight.scene_config;

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
        config.camera = {
            .auto_frame = false, .vertical_fov_degrees = 48, .position = {1500, 1680, 2200}, .target = {0, 20, 0}};
        config.scene.environment = {0.12, 0.12, 0.12};
        config.render.mode = RenderMode::Path;
        config.render.extent = {321, 241};
        config.render.ssaa = 2;
        config.render.path_samples = 16;
        config.render.max_bounces = 1;
        config.render.sampling = {.samples = 16, .seed = 7};
        config.lighting.emplace().lights = {{.type = LightKind::Rectangle,
                                             .center = {-1150, 1800, 350},
                                             .width = 550,
                                             .height = 550,
                                             .power = 35000000}};
    }
    return config;
}
} // namespace formlight
