import std;
import formlight.preview;

namespace
{
using namespace formlight;

// PNG (L,A) bytes are (192,0), (64,64), (128,160), (224,255).
// Only luminance is sRGB encoded. Even a fully transparent texel retains its color.
constexpr std::array<double, 4> gray{0.527115126, 0.051269458, 0.215860500, 0.745404210};
constexpr std::array<double, 4> opacity{0, 64.0 / 255, 160.0 / 255, 1};

void check_color(LinearColor actual, double r, double g, double b, double a, std::string_view stage, int index)
{
    const auto close = [](double value, double expected) { return std::abs(value - expected) < 1e-6; };
    if (!close(actual.r, r) || !close(actual.g, g) || !close(actual.b, b) || !close(actual.a, a))
    {
        throw std::runtime_error(std::format("{} {}: expected ({}, {}, {}, {}), got ({}, {}, {}, {})", stage, index, r,
                                             g, b, a, actual.r, actual.g, actual.b, actual.a));
    }
}

void check_panel(const GltfScene &scene)
{
    const auto &panel = scene.instance(InstanceId{0});
    const auto &face = panel.triangles.front();
    for (int i = 0; i < 4; ++i)
    {
        const double u = (i + 0.5) / 4;
        const auto color = scene.surface_material(face, {1 - u, u, 0}).base_color;
        check_color(color, gray[i], gray[i], gray[i], opacity[i], "texel", i);
    }

    const PinholeCamera camera{{{0, 0, 2}, {0, 0, 0}, {0, 1, 0}, std::numbers::pi / 2}, {64, 64}};
    const auto view = clip_to_bounds(camera, world_bounds(scene.instances()));
    const PreviewRenderer renderer{scene};
    const auto frame = renderer.render(view);
    const auto background = frame[0, 0];
    // The panel covers x=[16,48), with eight screen pixels per texel.
    for (int x = 16; x < 48; ++x)
    {
        const auto texel = std::clamp((x + 0.5 - 16) / 8 - 0.5, 0.0, 3.0);
        const int left = static_cast<int>(texel), right = std::min(left + 1, 3);
        const auto l = std::lerp(gray[left], gray[right], texel - left);
        const auto a = std::lerp(opacity[left], opacity[right], texel - left);
        const auto report = renderer.diagnose(view, {x, 32});
        if (report.surface.has_value() != (a > 0))
        {
            throw std::runtime_error(std::format("Unexpected BLEND pick at x={}", x));
        }
        if (report.surface)
        {
            check_color(report.surface->material.base_color, l, l, l, a, "filtered texel at x", x);
        }
        const auto r = std::lerp(double{background.r}, l, a);
        const auto g = std::lerp(double{background.g}, l, a);
        const auto b = std::lerp(double{background.b}, l, a);
        check_color(frame[32, x], r, g, b, 1, "frame at x", x);
        check_color(report.color, r, g, b, 1, "pixel replay at x", x);
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
    const auto scene = formlight::GltfScene::load(argv[1], formlight::GltfContent::Materials);
    if (!scene)
    {
        throw std::runtime_error(scene.error());
    }
    check_panel(*scene);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
