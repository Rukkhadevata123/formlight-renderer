import std;
import formlight.preview;

namespace
{
using namespace formlight;

// The dark panel starts a quarter unit to the right of the central pinhole ray.
double edge_visibility(double depth, int ssaa = 1)
{
    auto scene = GltfScene::empty_scene();
    scene.environment = {1, 1, 1};
    const TriangleVertex a{{0.25, -10, -depth}}, b{{10, -10, -depth}}, c{{10, 10, -depth}}, d{{0.25, 10, -depth}};
    scene.add_solid_mesh({Triangle{.vertices = {a, b, c}}, Triangle{.vertices = {a, c, d}}}, {0, 0, 0});
    const PreviewRenderer renderer{scene, {}, RenderMode::Path};
    const PinholeCamera camera{{{}, {0, 0, -1}, {0, 1, 0}, 0.5}, {1, 1}};
    PreviewView view{camera, {0.1, 20}};
    view.path_sampling = {.samples = 16384, .max_bounces = 0, .seed = 7};
    view.ssaa = ssaa;
    view.lens = ThinLens{10, 2 * std::atan(0.1) * 180 / std::numbers::pi}; // Unit aperture radius.
    const auto report = renderer.diagnose(view, {0, 0});
    if (report.color.r != renderer.render(view)[0, 0].r ||
        report.samples.size() != static_cast<std::size_t>(ssaa * ssaa) * view.path_sampling.samples)
    {
        throw std::runtime_error("Lens sample count or replayed pixel differs from the frame");
    }
    double sum = 0;
    for (const auto &sample : report.samples)
    {
        const Vec3 origin = sample.lens_origin.value();
        if (origin.z != 0 || dot(origin, origin) > 1.000000000001)
        {
            throw std::runtime_error("Lens sample outside aperture disk");
        }
        if (ssaa == 1 && sample.surface.has_value() != (origin.x * (1 - depth / 10) >= 0.25))
        {
            throw std::runtime_error("Lens ray hit the wrong side of the edge");
        }
        sum += sample.weight * sample.color.r;
    }
    if (std::abs(sum - report.color.r) > 1e-7)
    {
        throw std::runtime_error("Lens sample weights do not reproduce the pixel");
    }
    return report.color.r;
}
} // namespace

int main()
try
{
    // At depths 5 and 15 the footprint radius is 0.5. A circular segment beyond x=0.25 hits the panel.
    const double expected = 1 - (std::acos(0.5) - 0.5 * std::sqrt(0.75)) / std::numbers::pi;
    const double near = edge_visibility(5), focused = edge_visibility(10), far = edge_visibility(15);
    std::println("Edge visibility: near {:.5f}, focused {:.5f}, far {:.5f}, defocused reference {:.5f}", near, focused,
                 far, expected);
    const double tolerance = 5 * std::sqrt(expected * (1 - expected) / 16384);
    if (std::abs(near - expected) > tolerance || focused != 1 || std::abs(far - expected) > tolerance)
    {
        throw std::runtime_error("Defocused edge coverage differs from its circular aperture footprint");
    }
    (void)edge_visibility(5, 2);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
