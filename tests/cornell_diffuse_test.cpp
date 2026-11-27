import std;
import formlight.preview;
import formlight.scene_toml;

namespace
{
using namespace formlight;

Vec3 rgb(LinearColor color)
{
    return {color.r, color.g, color.b};
}

// Integrate the unobstructed ceiling patch in area measure at a white back-wall point.
double ceiling_reference(Vec3 receiver)
{
    constexpr int cells = 64;
    double sum = 0;
    for (int z = 0; z < cells; ++z)
    {
        for (int x = 0; x < cells; ++x)
        {
            const Vec3 point{0.96 * ((x + 0.5) / cells - 0.5), 1.995, 0.8 * ((z + 0.5) / cells - 0.5)};
            const auto d = point - receiver;
            const double r2 = dot(d, d);
            sum += d.z * d.y / (r2 * r2);
        }
    }
    return sum * (0.96 * 0.8 / (cells * cells)) * (8 * 0.73 / std::numbers::pi);
}

void check_back_wall(const PreviewRenderer &renderer, PreviewView view, PixelCoord pixel, bool red_wall)
{
    view.path_sampling = {.samples = 65536, .max_bounces = 1, .seed = 7};
    const auto direct = renderer.diagnose(view, pixel);
    const auto &surface = direct.samples.at(0).surface.value();
    const auto ray = view.camera.ray_for_sample(pixel_center(pixel.x, pixel.y));
    const Vec3 receiver = ray.at((-1 - ray.origin.z) / ray.direction.z);
    const double reference = ceiling_reference(receiver);
    const auto gray = rgb(direct.color);
    if (std::abs(surface.hit.view_depth - 4.8) > 1e-10 || gray.x != gray.y || gray.y != gray.z ||
        !(std::abs(gray.x - reference) < 0.015))
    {
        throw std::runtime_error(
            std::format("Back-wall pixel ({}, {}): direct {}, area integral {}", pixel.x, pixel.y, gray.x, reference));
    }

    view.path_sampling.max_bounces = 3;
    const auto multi = renderer.diagnose(view, pixel);
    const auto gain = rgb(multi.color) - gray;
    const double tint = red_wall ? gain.x - gain.y : gain.y - gain.x;
    if (!(tint > 0.025 && gain.z > 0) || !multi.samples.at(0).bounce_radiance ||
        rgb(renderer.diagnose(view, pixel).color) != rgb(multi.color))
    {
        throw std::runtime_error("Expected repeatable color bleeding from the adjacent wall");
    }
    std::println("({}, {}) direct {:.5f}, reference {:.5f}, three bounces ({:.5f}, {:.5f}, {:.5f})", pixel.x, pixel.y,
                 gray.x, reference, multi.color.r, multi.color.g, multi.color.b);
}
} // namespace

int main(int argc, char **argv)
try
{
    if (argc != 2)
    {
        return 1;
    }
    const auto config = load_scene_config(argv[1]);
    const auto request = prepare_render(config);
    const auto preview = load_preview(request);
    const PreviewRenderer renderer{preview.scene, request.lighting, request.mode};
    check_back_wall(renderer, preview.view, {32, 59}, true);
    check_back_wall(renderer, preview.view, {95, 59}, false);

    // View the same emitter from just below and just above it.
    for (bool below : {true, false})
    {
        const Vec3 eye{0, below ? 1.9 : 1.998, 0};
        const PinholeCamera camera{{eye, {0, 1.995, 0}, {0, 0, 1}, 0.5}, {1, 1}};
        const PreviewView view{camera, {0.0001, 4}};
        const double expected = below ? 8.0 : 0.0;
        if (rgb(renderer.render(view)[0, 0]) != Vec3{expected, expected, expected})
        {
            throw std::runtime_error("Only the downward-facing side of the ceiling lamp should emit");
        }
    }

    auto small = config;
    small.render.extent = {32, 32};
    small.render.ssaa = 2;
    const auto small_request = prepare_render(small);
    const PixelCoord pixel{10, 15};
    const auto frame = render_scene(small_request);
    const auto report = diagnose_pixel(small_request, pixel);
    if (report.samples.size() != 4 || rgb(frame[pixel.y, pixel.x]) != rgb(report.color) ||
        !report.samples.front().bounce_radiance || report.color.r <= 0)
    {
        throw std::runtime_error("SSAA did not replay the configured diffuse paths");
    }
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
