import std;
import formlight.preview;
import formlight.scene_toml;

namespace
{
using namespace formlight;

void check_segments()
{
    const std::array media{HomogeneousMedium{Sphere{{0, 0, 5}, 1, {4, 0, 0}}, 0.5}};
    const auto check = [&](Ray ray, RayInterval interval, double length)
    {
        const double t = medium_transmittance(media, ray, interval);
        if (std::abs(t - std::exp(-0.5 * length)) > 1e-12)
        {
            throw std::runtime_error(std::format("Incorrect transmittance for chord of length {}", length));
        }
    };
    check({{}, {0, 0, 2}}, {0, 10}, 2);
    check({{}, {0, 0, 2}}, {2.25, 2.75}, 1);
    check({{0, 0, 5}, {0, 0, 1}}, {0, 5}, 1);
    check({{}, {0, 0, 1}}, {0, 4}, 0);
    check({{1, 0, 0}, {0, 0, 1}}, {0, 6}, 0);
    check({{}, {0, 0, 1}, 1}, {0, 10}, 0);
    check({{4, 0, 0}, {0, 0, 1}, 1}, {0, 10}, 2);
    const std::array overlap{media.front(), media.front()};
    if (std::abs(medium_transmittance(overlap, {{}, {0, 0, 1}}, {0, 10}) - std::exp(-2.0)) > 1e-12)
    {
        throw std::runtime_error("Optical depths of overlapping media did not add");
    }
}

void check_light_connection(SceneConfig config)
{
    config.render.extent = {1, 1};
    config.render.path_samples = 1;
    config.scene.spheres = {{.shape = {{0, 0, -5}, 1}}};
    config.lighting.emplace().lights = {{.type = LightKind::Rectangle, .center = {0, 4, 0}}};
    config.scene.media.clear();
    const auto probe = [&]
    {
        const auto request = prepare_render(config);
        auto preview = load_preview(request);
        preview.view.area_light_pattern = AreaLightPattern::Center;
        const PreviewRenderer renderer{preview.scene, request.lighting, request.mode};
        return renderer.diagnose(preview.view, {0, 0}).samples.front().lights.at(0).direct.x;
    };
    const double clear = probe();
    // The center light sample runs from (0,0,-4) to (0,4,0). Only this segment crosses the absorber.
    config.scene.media.push_back({Sphere{{0, 2, -2}, 0.5}, 0.5});
    if (!(clear > 0) || std::abs(probe() / clear - std::exp(-0.5)) > 1e-12)
    {
        throw std::runtime_error("Rectangle light sample lost its medium transmittance");
    }
}

void check_primary(SceneConfig config)
{
    config.render.extent = {1, 1};
    config.render.path_samples = 32768;
    const auto request = prepare_render(config);
    const auto report = diagnose_pixel(request, {0, 0});
    const auto expected = std::exp(-1.0);
    if (report.samples.size() != config.render.path_samples || report.color.r != render_scene(request)[0, 0].r)
    {
        throw std::runtime_error("Primary medium samples or pixel replay differ from the frame");
    }
    unsigned first_unit_hits = 0;
    for (const auto &sample : report.samples)
    {
        if (sample.surface || sample.color.r != (sample.medium ? 0.0f : 1.0f))
        {
            throw std::runtime_error("Absorption and escape are not distinguished by the pixel report");
        }
        first_unit_hits += sample.medium && sample.medium->t < 5;
        if (sample.medium && (sample.medium->index != 0 || sample.medium->t < 4 || sample.medium->t >= 6))
        {
            throw std::runtime_error("Free-flight event fell outside the spherical boundary");
        }
    }
    const double first_unit_fraction = static_cast<double>(first_unit_hits) / config.render.path_samples;
    if (std::abs(first_unit_fraction - (1 - std::exp(-0.5))) > 0.014)
    {
        throw std::runtime_error("Free-flight positions do not follow exponential extinction inside the chord");
    }
    std::println("Diameter transmittance: {:.5f}, expected {:.5f}", report.color.r, expected);
    if (std::abs(report.color.r - expected) > 5 * std::sqrt(expected * (1 - expected) / config.render.path_samples))
    {
        throw std::runtime_error("Free-flight survival differs from Beer-Lambert transmittance");
    }

    // The mirror is in front of the medium. A reflected ray crosses only the medium behind the camera.
    config.scene.spheres = {{.shape = {{0, 0, -2}, 0.5}, .type = PathSurface::Mirror}};
    config.render.max_bounces = 1;
    if (render_scene(prepare_render(config))[0, 0].r != 1)
    {
        throw std::runtime_error("An absorber behind the nearest surface extinguished the primary ray");
    }
    config.scene.media.front().boundary.center = {0, 0, 5};
    const double reflected = render_scene(prepare_render(config))[0, 0].r;
    std::println("Reflected-ray transmittance: {:.5f}, expected {:.5f}", reflected, expected);
    if (std::abs(reflected - expected) > 0.015)
    {
        throw std::runtime_error("Reflected ray skipped the absorbing medium");
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
    check_segments();
    const auto config = formlight::load_scene_config(argv[1]);
    check_primary(config);
    check_light_connection(config);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
