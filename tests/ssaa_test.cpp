import std;
import formlight.preview;

namespace
{
using namespace formlight;

struct ExpectedSample
{
    Vec3 color;
    std::optional<InstanceId> instance;
    double depth{};
};

void check_color(std::string_view label, LinearColor actual, Vec3 expected)
{
    const auto d = abs_components(Vec3{actual.r, actual.g, actual.b} - expected);
    if (!(std::max({d.x, d.y, d.z}) < 1e-6 && actual.a == 1))
    {
        throw std::runtime_error(std::format("{}: expected ({}, {}, {}), got ({}, {}, {}, {})", label, expected.x,
                                             expected.y, expected.z, actual.r, actual.g, actual.b, actual.a));
    }
}

void check_edge(std::string_view label, const PreviewRenderer &renderer, Vec3 edge, ExpectedSample left,
                ExpectedSample right)
{
    // Aim just to the right of a vertical edge. The center hits the right side, but two quarter samples hit each side.
    const Vec3 target = edge + Vec3{0.01, 0, 0};
    const PinholeCamera camera{{target + Vec3{0, 0, 4}, target, {0, 1, 0}, std::numbers::pi / 6}, {33, 25}};
    PreviewView view{camera, {1, 6}};
    constexpr PixelCoord pixel{16, 12};
    check_color(label, renderer.render(view)[pixel.y, pixel.x], right.color);

    view.ssaa = 2;
    const auto frame = renderer.render(view);
    const auto report = renderer.diagnose(view, pixel);
    if (report.samples.size() != 4 || frame.extent().width != 33 || frame.extent().height != 25)
    {
        throw std::runtime_error(std::format("{}: incorrect output dimensions or sample count", label));
    }
    for (int i = 0; i < 4; ++i)
    {
        const auto &sample = report.samples[i];
        const auto &expected = i % 2 == 0 ? left : right;
        const auto instance = sample.surface ? std::optional{sample.surface->hit.surface.id.instance} : std::nullopt;
        if (sample.position.x != pixel.x + 0.25 + 0.5 * (i % 2) ||
            sample.position.y != pixel.y + 0.25 + 0.5 * (i / 2) || sample.weight != 0.25 ||
            instance != expected.instance ||
            (sample.surface && std::abs(sample.surface->hit.view_depth - expected.depth) > 1e-9))
        {
            throw std::runtime_error(
                std::format("{}: incorrect position, weight or visible surface for sample {}", label, i));
        }
        check_color(label, sample.color, expected.color);
    }
    const auto mean = (left.color + right.color) / 2;
    check_color(label, frame[pixel.y, pixel.x], mean);
    check_color(label, report.color, mean);
}
} // namespace

int main(int argc, char **argv)
try
{
    if (argc != 3)
    {
        return 1;
    }
    auto panels = GltfScene::load(argv[1], GltfContent::Materials);
    const auto emissive = GltfScene::load(argv[2], GltfContent::Materials);
    if (!panels || !emissive)
    {
        throw std::runtime_error(!panels ? panels.error() : emissive.error());
    }
    // The mask cutoff is halfway across panel 2. A green triangle sits one unit behind it.
    const TriangleVertex a{{-4, -4, -1}}, b{{2, -4, -1}}, c{{-1, 2, -1}};
    panels->add_solid_mesh({Triangle{.vertices = {a, b, c}}}, {0, 1, 0});
    check_edge("mask over opaque", PreviewRenderer{*panels}, {-1.2, -1.2, 0}, {{0, 1, 0}, InstanceId{4}, 5},
               {{1, 1, 1}, InstanceId{2}, 4});

    // With no incident light, these panels contribute only their known linear emission.
    const PreviewRenderer renderer{*emissive, PreviewLighting{}};
    const Vec3 amber{0.25, 0.125, 0.0625};
    check_edge("opaque silhouette", renderer, {-1.5, 0, 0}, {{0.035, 0.045, 0.06}, {}}, {amber, InstanceId{0}, 4});
    const Vec3 warm = Vec3{0.21586050 * 0.5, 0.05126946, 0.01444384 * 0.75} * 4;
    check_edge("blend over opaque", renderer, {0, 0, 0.0625}, {amber, InstanceId{0}, 4.0625},
               {(warm + amber) / 2, InstanceId{1}, 4});
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
