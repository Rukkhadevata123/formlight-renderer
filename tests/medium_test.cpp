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

// Independent midpoint quadrature for a unit sphere with extinction 0.5, seen along its diameter.
// Integrate the first collision depth and all outgoing polar angles, with no further collision.
double one_scatter_escape()
{
    constexpr unsigned steps = 256;
    constexpr double sigma = 0.5;
    double sum = 0;
    for (unsigned i = 0; i < steps; ++i)
    {
        const double s = (i + 0.5) * 2 / steps;
        const double z = s - 1;
        for (unsigned j = 0; j < steps; ++j)
        {
            const double mu = (j + 0.5) * 2 / steps - 1;
            const double exit = std::sqrt(1 - z * z * (1 - mu * mu)) - z * mu;
            sum += sigma * std::exp(-sigma * (s + exit));
        }
    }
    return sum * 2 / (steps * steps);
}

void check_scattering(SceneConfig config)
{
    config.render.extent = {1, 1};
    config.render.path_samples = 32768;
    config.render.max_bounces = 1;
    auto &albedo = config.scene.media.front().scattering_albedo;
    albedo = {0, 0.5, 1};
    const auto color = render_scene(prepare_render(config))[0, 0];
    const double ballistic = std::exp(-1.0);
    const double scattered = one_scatter_escape();
    std::println("One bounce RGB: {:.5f}, {:.5f}, {:.5f}; reference {:.5f}, {:.5f}, {:.5f}", color.r, color.g, color.b,
                 ballistic, ballistic + 0.5 * scattered, ballistic + scattered);
    if (std::abs(color.r - ballistic) > 0.014 || std::abs(color.g - ballistic - 0.5 * scattered) > 0.014 ||
        std::abs(color.b - ballistic - scattered) > 0.014)
    {
        throw std::runtime_error("Single-scattered radiance differs from integration of the isotropic phase function");
    }

    albedo = {1, 1, 1};
    config.render.max_bounces = 32;
    const auto white = render_scene(prepare_render(config))[0, 0];
    std::println("Conservative medium in white environment: {:.6f}, {:.6f}, {:.6f}", white.r, white.g, white.b);
    if (white.r < 0.9999f || white.r > 1 || white.r != white.g || white.g != white.b)
    {
        throw std::runtime_error("Multiple scattering did not conserve uniform incident radiance");
    }

    for (const double invalid : {-0.1, 1.01, std::numeric_limits<double>::quiet_NaN()})
    {
        albedo.y = invalid;
        try
        {
            (void)prepare_render(config);
        }
        catch (const std::invalid_argument &)
        {
            continue;
        }
        throw std::runtime_error("Invalid medium scattering albedo accepted");
    }
}

void check_blue_glass_and_fog(SceneConfig config)
{
    config.render.path_samples = 8192;
    const PixelCoord center{config.render.extent.width / 2, config.render.extent.height / 2};
    const auto report = diagnose_pixel(prepare_render(config), center);
    std::size_t fog_hits = 0;
    for (const auto &sample : report.samples)
    {
        if (sample.medium && sample.medium->index == 1)
        {
            ++fog_hits;
        }
        else if (!sample.surface)
        {
            throw std::runtime_error("Central ray must encounter the global fog or the front glass interface first");
        }
    }
    const auto &glass = config.scene.spheres.front().shape;
    const Vec3 delta = glass.center - config.camera.position;
    const double entry = std::sqrt(dot(delta, delta)) - glass.radius;
    const double expected = -std::expm1(-config.scene.media.at(1).extinction * entry);
    const double fraction = static_cast<double>(fog_hits) / config.render.path_samples;
    std::println("Fog before glass: {:.5f}, expected {:.5f}; center RGB {:.5f}, {:.5f}, {:.5f}", fraction, expected,
                 report.color.r, report.color.g, report.color.b);
    if (std::abs(fraction - expected) > 5 * std::sqrt(expected * (1 - expected) / config.render.path_samples) ||
        !(report.color.r < report.color.g && report.color.g < report.color.b))
    {
        throw std::runtime_error("Fog collision probability or blue glass transmission differs from the fixture");
    }
}

void check_volume_light(SceneConfig config)
{
    config.scene.environment = {};
    config.render.extent = {1, 1};
    config.render.path_samples = 256;
    config.render.max_bounces = 0;
    config.scene.media.front().scattering_albedo = {0.25, 0.5, 1};
    config.lighting.emplace().lights = {{.type = LightKind::Rectangle, .center = {0, 2, -5}, .power = 12}};
    const auto request = prepare_render(config);
    auto preview = load_preview(request);
    preview.view.area_light_pattern = AreaLightPattern::Center;
    const PreviewRenderer renderer{preview.scene, request.lighting, request.mode};
    const auto report = renderer.diagnose(preview.view, {0, 0});
    std::size_t collisions = 0;
    for (const auto &sample : report.samples)
    {
        if (!sample.medium)
        {
            continue;
        }
        ++collisions;
        const double z = 5 - sample.medium->t;
        const double d = std::hypot(2.0, z);
        const double projection = -z * z / d;
        const double exit = std::sqrt(1 - z * z + projection * projection) - projection;
        const double expected =
            (12 / std::numbers::pi) * (2 / d) / (d * d) * std::exp(-0.5 * exit) / (4 * std::numbers::pi);
        if (std::abs(sample.color.b - expected) > 1e-7 || sample.color.g != sample.color.b * 0.5f ||
            sample.color.r != sample.color.b * 0.25f)
        {
            throw std::runtime_error("Volume light connection differs from the attenuated isotropic response");
        }
    }
    if (collisions == 0 || renderer.render(preview.view)[0, 0].b != report.color.b)
    {
        throw std::runtime_error("Volume light samples did not reproduce the frame");
    }

    // Block every connection to the light center, leaving the primary ray unobstructed.
    config.scene.spheres = {{.shape = {{0, 2, -5}, 0.5}}};
    auto blocked = load_preview(prepare_render(config));
    blocked.view.area_light_pattern = AreaLightPattern::Center;
    if (PreviewRenderer{blocked.scene, request.lighting, request.mode}.render(blocked.view)[0, 0].b != 0)
    {
        throw std::runtime_error("Occluded rectangle light illuminated the medium");
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
    if (argc != 3)
    {
        return 1;
    }
    check_segments();
    const auto config = formlight::load_scene_config(argv[1]);
    check_primary(config);
    check_light_connection(config);
    check_scattering(config);
    check_volume_light(config);
    check_blue_glass_and_fog(formlight::load_scene_config(argv[2]));
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
