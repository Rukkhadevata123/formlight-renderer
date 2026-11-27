import std;
import formlight.preview;
import formlight.scene_toml;

using namespace formlight;

namespace
{
void check_light_sum(SceneConfig config, const ScenePreview &preview)
{
    auto &settings = config.lighting.value();
    settings.ambient = {0.25, 0.125, 0.5};
    settings.lights = {{.type = LightKind::Directional, .toward_light = {1, 0, 2}, .color = {1, 0.25, 0}},
                       {.type = LightKind::Point, .shadows = false, .position = {-1, 1, 2}, .color = {0, 0.25, 1}}};
    const auto frame = [&](bool first, bool second)
    {
        settings.lights[0].enabled = first;
        settings.lights[1].enabled = second;
        return render_preview(preview.scene, preview.view, prepare_render(config).lighting);
    };
    const auto baseline = frame(false, false);
    const auto first = frame(true, false);
    const auto second = frame(false, true);
    const auto combined = frame(true, true);
    for (int y = 0; y < combined.extent().height; ++y)
    {
        for (int x = 0; x < combined.extent().width; ++x)
        {
            const auto rgb = [=](const ColorImage &image)
            {
                const auto c = image[y, x];
                return Vec3{c.r, c.g, c.b};
            };
            const auto delta = abs_components(rgb(combined) - (rgb(first) + rgb(second) - rgb(baseline)));
            if (!(std::max({delta.x, delta.y, delta.z}) < 2e-6))
            {
                throw std::runtime_error(
                    std::format("Two lights disagree with their individual renders at ({}, {})", x, y));
            }
        }
    }
    const auto report = diagnose_pixel(preview.scene, preview.view, {38, 16}, prepare_render(config).lighting);
    if (report.lights.size() != 2 || report.lights[0].direct.x <= 0 || report.lights[1].direct.z <= 0 ||
        report.sample_contribution(0, 0) != report.lights[0].direct * 0.5 ||
        report.sample_contribution(1, 0) != report.lights[1].direct * 0.5)
    {
        throw std::runtime_error("The translucent layer must report each light with its own sample weight");
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
    auto config = load_scene_config(argv[1]);
    config.render.extent = {64, 32};
    const auto preview = load_preview(prepare_render(config));
    const Vec3 amber{0.25, 0.125, 0.0625};
    // The sheet has strength 4. Its first emissive texel is sRGB (128, 64, 32) with zero alpha.
    const Vec3 warm = Vec3{0.21586050 * 0.5, 0.05126946, 0.01444384 * 0.75} * 4;
    const Vec3 cyan{0, 4, 3};
    for (const auto mode : {RenderMode::Raster, RenderMode::Hybrid})
    {
        config.render.mode = mode;
        const PreviewRenderer renderer{preview.scene, prepare_render(config).lighting};
        const auto frame = renderer.render(preview.view);
        const auto check = [&](int x, Vec3 expected)
        {
            const auto report = renderer.diagnose(preview.view, {x, 16});
            const auto color = frame[16, x];
            const auto delta = abs_components(Vec3{color.r, color.g, color.b} - expected);
            if (std::max({delta.x, delta.y, delta.z}) > 1e-6 || report.lights.at(0).samples.size() != 1 ||
                report.sample_contribution(0, 0) != Vec3{} ||
                std::tie(color.r, color.g, color.b) != std::tie(report.color.r, report.color.g, report.color.b))
            {
                throw std::runtime_error(std::format("Incorrect emission or direct light at ({}, 16)", x));
            }
        };
        check(20, amber);
        check(38, (warm + amber) * 0.5);
        check(50, (cyan + amber) * 0.5);
        check_light_sum(config, preview);
    }
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
