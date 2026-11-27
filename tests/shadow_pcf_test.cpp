import std;
import formlight.preview;
import formlight.shadow_map;

namespace
{
using namespace formlight;

void check_sloped_receiver()
{
    const Triangle face{
        .vertices = {TriangleVertex{{-2, -2, -3}}, TriangleVertex{{2, -2, 5}}, TriangleVertex{{0, 2, -1}}}};
    const std::array instances{MeshInstance{InstanceId{0}, std::span{&face, 1}}};
    const DirectionalShadowMap map{instances, DirectionalLight{{0, 0, 1}}};
    const Vec3 normal = unit_vector(face.geometric_normal(), "Degenerate receiver");
    for (int i = 1; i < 32; ++i)
    {
        for (int j = 1; i + j < 32; ++j)
        {
            const Vec3 point = face.position_at({(32 - i - j) / 32.0, i / 32.0, j / 32.0});
            if (map.visibility(point, normal, true) != 1.0)
            {
                throw std::runtime_error(std::format("PCF self-shadow on tilted receiver at ({}, {})", i, j));
            }
        }
    }
}

void check_contact_edge(GltfScene &scene)
{
    // The lower left corner of panel 1 aligns with the center of the light map at (0.2, 0.2).
    constexpr double gap = 1e-5;
    const TriangleVertex a{{-3.8, -3.8, -gap}}, b{{4.2, -3.8, -gap}}, c{{0.2, 4.2, -gap}};
    scene.add_solid_mesh({Triangle{.vertices = {a, b, c}}}, {1, 1, 1});
    const auto receiver = scene.instances().back().id;
    // Clip the panel for the camera. The 4x4 receiver samples lie in consecutive shadow texels around the corner.
    const PinholeCamera camera{{{0.2, 0.2, 1}, {0.2, 0.2, 0}, {0, 1, 0}, 2 * std::atan(0.024)}, {4, 4}};
    const PreviewView view{camera, {1 + gap / 2, 2}};
    constexpr Vec3 ambient{0.125, 0.25, 0.5};
    LightSettings light{.toward_light = {0, 0, 1}};
    RenderSettings render;
    PreviewRenderer renderer{scene};
    const auto relight = [&] { renderer.set_lighting(PreviewLighting{prepare_light(light, render), ambient}); };
    light.shadows = false;
    relight();
    const auto unshadowed = renderer.render(view);
    light.shadows = true;
    for (const auto mode : {RenderMode::Raster, RenderMode::Hybrid})
    {
        render.mode = mode;
        for (bool pcf : {false, true})
        {
            light.pcf = pcf;
            relight();
            const auto frame = renderer.render(view);
            const bool filtered = mode == RenderMode::Raster && pcf;
            for (int y = 0; y < 4; ++y)
            {
                for (int x = 0; x < 4; ++x)
                {
                    // The upper-right quadrant is blocked. The 3x3 kernel overlaps x columns and (3-y) rows of it.
                    const double hard = x >= 2 && y < 2 ? 0.0 : 1.0;
                    const double expected = filtered ? (9 - x * (3 - y)) / 9.0 : hard;
                    const auto report = renderer.diagnose(view, {x, y});
                    const auto &sample = report.samples.at(0);
                    const auto &shadow = sample.lights.at(0).samples.at(0).shadow;
                    if (!sample.surface || sample.surface->hit.surface.id.instance != receiver ||
                        shadow.visibility != expected ||
                        (shadow.state == ShadowState::PartiallyVisible) != (expected > 0 && expected < 1))
                    {
                        throw std::runtime_error(std::format("Pixel ({}, {}): expected visibility {}, got {}", x, y,
                                                             expected, shadow.visibility));
                    }
                    const auto lit = unshadowed[y, x];
                    const Vec3 rgb = ambient + (Vec3{lit.r, lit.g, lit.b} - ambient) * expected;
                    const auto color = frame[y, x];
                    const auto error = abs_components(Vec3{color.r, color.g, color.b} - rgb);
                    if (!(std::max({error.x, error.y, error.z}) < 2e-6) ||
                        std::tie(color.r, color.g, color.b) != std::tie(report.color.r, report.color.g, report.color.b))
                    {
                        throw std::runtime_error(
                            std::format("Pixel ({}, {}): incorrect filtered direct light or replay", x, y));
                    }
                }
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
    auto scene = GltfScene::load(argv[1], GltfContent::Materials);
    if (!scene)
    {
        throw std::runtime_error(scene.error());
    }
    check_sloped_receiver();
    check_contact_edge(*scene);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
