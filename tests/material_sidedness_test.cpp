import std;
import formlight.preview;

namespace
{
using namespace formlight;

void check_view(const GltfScene &scene, bool from_back, ShadowMethod shadows)
{
    const double side = from_back ? -1.0 : 1.0;
    const Vec3 light{0, 0, side};
    const PinholeCamera camera{{{0, 0, 6 * side}, {0, 0, 0}, {0, 1, 0}, std::numbers::pi / 3}, {128, 128}};
    const auto view = clip_to_bounds(camera, world_bounds(scene.instances()));
    const PreviewRenderer renderer{scene, DirectionalLighting{DirectionalLight{light}, shadows}};
    const auto frame = renderer.render(view);
    const auto clear = frame[0, 0];
    for (const auto &panel : scene.instances())
    {
        const auto id = std::to_underlying(panel.id);
        const bool masked = id >= 2;
        const bool double_sided = id == 1 || id == 3;
        const bool mirrored = id == 0 || id == 3;
        const Vec3 normal = Vec3{mirrored ? -0.6 : 0.6, 0, 0.8} * side;
        // Vertex alpha rises from zero on the local left edge to one on the right.
        for (const double x : {-0.5, 0.5})
        {
            const auto world = panel.transform.to_world_point({x, 0, 0});
            const auto p = camera.project(world).value().image;
            const PixelCoord pixel{static_cast<int>(p.x), static_cast<int>(p.y)};
            const auto color = frame[pixel.y, pixel.x];
            const auto report = renderer.diagnose(view, pixel);
            const auto &sample = report.samples.at(0);
            const auto label = std::format("panel {}, local x {}, eye z {}", id, x, camera.view().position.z);
            if (std::tie(color.r, color.g, color.b) != std::tie(report.color.r, report.color.g, report.color.b))
            {
                throw std::runtime_error(label + ": frame and pixel replay differ");
            }
            if ((from_back && !double_sided) || (masked && x < 0))
            {
                if (sample.surface || std::tie(color.r, color.g, color.b) != std::tie(clear.r, clear.g, clear.b))
                {
                    throw std::runtime_error(label + ": expected background");
                }
                continue;
            }
            if (!sample.surface || sample.surface->hit.surface.id.instance != panel.id ||
                sample.surface->hit.back_face != from_back)
            {
                throw std::runtime_error(label + ": wrong surface or facing");
            }
            const auto dn = abs_components(sample.surface->shading_normal.value() - normal);
            if (!(std::max({dn.x, dn.y, dn.z}) < 1e-6))
            {
                throw std::runtime_error(label + ": incorrect world shading normal");
            }
            const Vec3 to_eye = camera.ray_for_sample(pixel_center(pixel.x, pixel.y)).direction * -1;
            const MetallicRoughnessBrdf brdf{sample.surface->material, normal, to_eye};
            const auto expected = brdf.evaluate_times_cosine(light) * std::numbers::pi;
            const auto delta = abs_components(Vec3{color.r, color.g, color.b} - expected);
            if (!(std::max({delta.x, delta.y, delta.z}) < 1e-6))
            {
                throw std::runtime_error(label + ": incorrect direct lighting");
            }
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
    const auto scene = formlight::GltfScene::load(argv[1], formlight::GltfContent::Materials);
    if (!scene || scene->instances().size() != 4)
    {
        throw std::runtime_error(scene ? "Expected four panels" : scene.error());
    }
    for (const auto shadows : {formlight::ShadowMethod::Map, formlight::ShadowMethod::Ray})
    {
        check_view(*scene, false, shadows);
        check_view(*scene, true, shadows);
    }
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
