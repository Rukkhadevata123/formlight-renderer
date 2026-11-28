import std;
import formlight.preview;
import formlight.scene_toml;

namespace
{
using namespace formlight;

void check_exposure(SceneConfig config)
{
    config.render.extent = {1, 1};
    config.render.path_samples = 16384;
    const auto request = prepare_render(config);
    const auto report = diagnose_pixel(request, {0, 0});
    if (report.samples.size() != config.render.path_samples || report.color.r != render_scene(request)[0, 0].r)
    {
        throw std::runtime_error("Shutter samples or replayed exposure differ from the frame");
    }
    for (const auto &sample : report.samples)
    {
        // A radius-one sphere covers x=0 while its center travels from -1 to 1.
        const double time = sample.time.value();
        const bool covered = time >= 0.25 && time <= 0.75;
        if (!(time >= 0 && time <= 1) || sample.surface.has_value() != covered ||
            sample.color.r != (covered ? 0.0f : 1.0f))
        {
            throw std::runtime_error("Recorded time does not reproduce moving silhouette");
        }
    }
    std::println("Center pixel background visibility: {:.5f} (reference 0.5)", report.color.r);
    if (std::abs(report.color.r - 0.5) > 5 * 0.5 / std::sqrt(config.render.path_samples))
    {
        throw std::runtime_error("Uniform exposure differs from the analytic silhouette coverage");
    }
}

void check_lens_and_shutter(SceneConfig config)
{
    config.render.extent = {81, 81};
    config.render.ssaa = 2;
    config.render.path_samples = 16;
    config.camera.lens = {.focus_distance = 5, .defocus_angle_degrees = 1};
    config.camera.shutter = {0.4, 0.6};
    const auto report = diagnose_pixel(prepare_render(config), {40, 40});
    if (report.samples.size() != 4 * config.render.path_samples)
    {
        throw std::runtime_error("Joint lens and shutter sampling used the wrong number of camera rays");
    }
    for (const auto &sample : report.samples)
    {
        if (!sample.lens_origin || !sample.surface || sample.time.value() < 0.4 || sample.time.value() > 0.6 ||
            sample.weight != 1.0 / static_cast<double>(report.samples.size()))
        {
            throw std::runtime_error("Joint camera sample lost its lens point, shutter interval or SSAA weight");
        }
    }
}

void check_frozen_glass(SceneConfig config)
{
    config.render.extent = {1, 1};
    config.render.max_bounces = 1;
    config.render.path_samples = 1024;
    config.camera.shutter = {0.5, 0.5};
    auto &sphere = config.scene.spheres.at(0);
    sphere.type = PathSurface::Dielectric;
    const auto frozen = diagnose_pixel(prepare_render(config), {0, 0});
    sphere.shape.center = {0, 0, -5};
    sphere.shape.displacement = {};
    config.camera.shutter = {};
    const auto reference = diagnose_pixel(prepare_render(config), {0, 0});
    // Only first-interface Fresnel reflections reach the white sky within one bounce.
    if (frozen.samples.size() != 1 || frozen.samples.front().time != 0.5 || frozen.color.r != reference.color.r ||
        !(frozen.color.r > 0 && frozen.color.r < 0.1))
    {
        throw std::runtime_error("Glass continuation at fixed shutter time differs from its static sphere");
    }
}
} // namespace

int main(int argc, char **argv)
try
{
    if (argc != 2)
    {
        return 1;
    }
    const auto config = formlight::load_scene_config(argv[1]);
    check_exposure(config);
    check_frozen_glass(config);
    check_lens_and_shutter(config);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
