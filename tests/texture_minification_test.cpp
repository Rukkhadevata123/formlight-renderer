import std;
import formlight.preview;
import formlight.scene_toml;

namespace
{
using namespace formlight;

void check(double actual, double expected, std::string_view label, double tolerance = 1e-6)
{
    if (!(std::abs(actual - expected) <= tolerance))
    {
        throw std::runtime_error(std::format("{}: expected {}, got {}", label, expected, actual));
    }
}

void check_filters()
{
    ColorImage strip{{4, 1}};
    strip[0, 1].r = 0.25f;
    strip[0, 2].r = 1;
    strip[0, 3].r = 0.75f;
    TextureImage image{std::move(strip)};
    image.generate_mipmaps();
    // At u=0.4, linear samples are 0.325 on level zero and 0.35 on level one.
    // The long footprint axis is screen Y. It spans 2^0.6 horizontal texels.
    const UvGradients footprint{{}, {std::exp2(0.6) / 4, 0}};
    for (auto min : {TexelFilter::Nearest, TexelFilter::Linear})
    {
        const bool nearest = min == TexelFilter::Nearest;
        const double level0 = nearest ? 0.25 : 0.325, level1 = nearest ? 0.125 : 0.35;
        TextureSampler sampler{.mag = TexelFilter::Nearest, .min = min};
        const auto sample = [&] { return image.sample({0.4, 0.5}, sampler, footprint).r; };
        check(sample(), level0, "Minify on level zero");
        sampler.mip = MipFilter::Nearest;
        check(sample(), level1, "Nearest mip");
        sampler.mip = MipFilter::Linear;
        check(sample(), std::lerp(level0, level1, 0.6), "Blend adjacent mips");
        check(image.sample({0.4, 0.5}, sampler, {{0, 0.75}, {}}).r, 0.25, "Magnify on a 4x1 texture");
    }

    ColorImage odd{{5, 3}, {0, 0, 0, 0}};
    odd[2, 4] = {1, 1, 1, 1};
    TextureImage corner{std::move(odd)};
    corner.generate_mipmaps();
    const auto mean = corner.sample({}, {.mip = MipFilter::Linear}, {{100, 0}, {}});
    check(mean.r, 1.0 / 15, "Odd mip retains corner");
    check(mean.a, 1.0 / 15, "Odd mip averages alpha");
}

void check_floor(const SceneConfig &config)
{
    auto preview = load_preview(prepare_render(config));
    // Clip the long floor at both ends. The gradients still refer to its original UV1.
    preview.view.depth = {2, 18};
    const auto &camera = preview.view.camera;
    const PreviewRenderer renderer{preview.scene};
    const auto frame = renderer.render(preview.view);
    const auto floor_uv = [&](double x, double y) -> TexCoord
    {
        const auto ray = camera.ray_for_sample({x, y});
        const auto p = ray.at(-ray.origin.y / ray.direction.y);
        return {(p.x + 2) / 2, (2 - p.z) / 2};
    };
    for (int y : {125, 180, 220})
    {
        const PixelCoord pixel{250, y};
        const auto report = renderer.diagnose(preview.view, pixel);
        const auto &surface = report.surface.value();
        const auto &point = surface.hit.surface;
        const auto &face = preview.scene.instance(point.id.instance).triangles[std::to_underlying(point.id.primitive)];
        const auto grad = face.uv_gradients(point.gradients, 1);
        const auto center = pixel_center(pixel.x, pixel.y);
        constexpr double h = 1e-4;
        const auto left = floor_uv(center.x - h, center.y), right = floor_uv(center.x + h, center.y);
        const auto top = floor_uv(center.x, center.y - h), bottom = floor_uv(center.x, center.y + h);
        check(grad.dx.u, (right.u - left.u) / (2 * h), "du/dx", 1e-8);
        check(grad.dx.v, (right.v - left.v) / (2 * h), "dv/dx", 1e-8);
        check(grad.dy.u, (bottom.u - top.u) / (2 * h), "du/dy", 1e-8);
        check(grad.dy.v, (bottom.v - top.v) / (2 * h), "dv/dy", 1e-8);
        check(report.color.r, frame[y, pixel.x].r, "Pixel replay", 0);
        if (y == 125)
        {
            check(report.color.r, 0.37992403, "Distant sRGB checker averaged in linear space");
            check(surface.material.roughness, 128.0 / 255, "Distant data texture averaged without sRGB decoding");
        }
        if (y == 220)
        {
            check(report.color.r, 0.74540421, "Near checker magnification");
        }
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
    check_filters();
    check_floor(formlight::load_scene_config(argv[1]));
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
