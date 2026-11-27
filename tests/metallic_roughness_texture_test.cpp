import std;
import formlight.preview;

using namespace formlight;

namespace
{
void check_lighting(const GltfScene &scene, Vec3 base)
{
    const PinholeCamera camera{{{0, 0, 3}, {}, {0, 1, 0}, std::numbers::pi / 4}, {64, 64}};
    const auto view = clip_to_bounds(camera, world_bounds(scene.instances()));
    const PixelCoord pixel{13, 32}; // The clamped left texel.
    const Vec3 ambient{0.8, 0.6, 0.4};
    const Vec3 expected_ambient =
        Vec3{ambient.x * base.x, ambient.y * base.y, ambient.z * base.z} * (0.75 + 0.25 * 32 / 255);

    const auto frame_color = [&](PreviewLighting lighting)
    {
        const PreviewRenderer renderer{scene, lighting};
        const auto color = renderer.render(view)[pixel.y, pixel.x];
        const auto report = renderer.diagnose(view, pixel);
        if (!report.surface ||
            std::tie(color.r, color.g, color.b) != std::tie(report.color.r, report.color.g, report.color.b))
        {
            throw std::runtime_error("Panel pixel inspection disagrees with rendering");
        }
        return Vec3{color.r, color.g, color.b};
    };
    const auto check_rgb = [](Vec3 actual, Vec3 expected, const char *label)
    {
        const auto delta = abs_components(actual - expected);
        if (!(std::max({delta.x, delta.y, delta.z}) < 1e-6))
        {
            throw std::runtime_error(std::format("{}: got ({}, {}, {})", label, actual.x, actual.y, actual.z));
        }
    };
    check_rgb(frame_color({std::monostate{}, ambient}), expected_ambient, "Ambient only");

    const DirectionalLighting direct{DirectionalLight{{0.3, 0.4, 1}}};
    const Vec3 eye = camera.ray_for_sample(pixel_center(pixel.x, pixel.y)).direction * -1;
    const SurfaceMaterial reference{
        {static_cast<float>(base.x), static_cast<float>(base.y), static_cast<float>(base.z)},
        {},
        0.5 * 192 / 255,
        0.5 * 64 / 255};
    const MetallicRoughnessBrdf brdf{reference, {0, 0, 1}, eye};
    const auto incident = direct.light.incident();
    const auto expected_direct = brdf.evaluate_times_cosine(incident.direction) * incident.weight;
    check_rgb(frame_color(direct), expected_direct, "Direct only");
    check_rgb(frame_color({direct, ambient}), expected_direct + expected_ambient, "Ambient and direct");
}
} // namespace

int main(int argc, char **argv)
try
{
    if (argc != 2)
    {
        return 1;
    }
    const auto scene = GltfScene::load_glb(argv[1], GltfContent::Materials);
    if (!scene)
    {
        throw std::runtime_error(scene.error());
    }
    const auto &face = scene->instances().front().triangles.front();
    const auto check = [&](double u, Vec3 expected_color, double red, double green, double blue)
    {
        const auto surface = scene->surface_material(face, {1 - u, u / 2, u / 2});
        const auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };
        if (!near(surface.roughness, 0.5 * green / 255) || !near(surface.metallic, 0.5 * blue / 255))
        {
            throw std::runtime_error(
                std::format("At u={}, roughness={}, metallic={}", u, surface.roughness, surface.metallic));
        }
        const auto color = surface.base_color;
        if (!near(surface.occlusion, 0.75 + 0.25 * red / 255))
        {
            throw std::runtime_error(std::format("At u={}, occlusion={}", u, surface.occlusion));
        }
        if (!near(color.r, expected_color.x) || !near(color.g, expected_color.y) || !near(color.b, expected_color.z))
        {
            throw std::runtime_error(std::format("At u={}, base color=({}, {}, {})", u, color.r, color.g, color.b));
        }
    };

    // All bindings reference the same two texels. Filter color after sRGB decoding and data in linear space.
    const Vec3 left{0.01444384, 0.05126946, 0.52711513};
    const Vec3 right{0.74540421, 0.35153260, 0.02955683};
    check(0.25, left, 32, 64, 192);
    check(0.75, right, 224, 160, 48);
    check(0.5, (left + right) * 0.5, 128, 112, 120);
    check_lighting(*scene, left);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
