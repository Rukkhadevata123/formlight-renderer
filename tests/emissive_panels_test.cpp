import std;
import formlight.preview;
import formlight.scene_toml;

using namespace formlight;

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
            if (std::max({delta.x, delta.y, delta.z}) > 1e-6 || report.samples.size() != 1 ||
                report.sample_contribution(0) != Vec3{} ||
                std::tie(color.r, color.g, color.b) != std::tie(report.color.r, report.color.g, report.color.b))
            {
                throw std::runtime_error(std::format("Incorrect emission or direct light at ({}, 16)", x));
            }
        };
        check(20, amber);
        check(38, (warm + amber) * 0.5);
        check(50, (cyan + amber) * 0.5);
    }
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
