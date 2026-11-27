import std;
import formlight.preview;
import formlight.scene_toml;

namespace
{
using namespace formlight;

constexpr Vec3 amber{0.25, 0.125, 0.0625};

void check_rgb(LinearColor color, Vec3 expected)
{
    if (Vec3{color.r, color.g, color.b} != expected || color.a != 1)
    {
        throw std::runtime_error(std::format("Expected ({}, {}, {}), got ({}, {}, {}, {})", expected.x, expected.y,
                                             expected.z, color.r, color.g, color.b, color.a));
    }
}

void check_emitter(SceneConfig config)
{
    config.render.mode = parse_choice<RenderMode>("path");
    config.render.extent = {64, 32};
    const auto request = prepare_render(config);
    const auto image = render_scene(request);
    // The backlit panel is at z=0, with a two-triangle diagonal from bottom left to top right.
    for (const auto [x, primitive] : {std::pair{20, 1u}, std::pair{38, 0u}})
    {
        const auto report = diagnose_pixel(request, {x, 16});
        const auto &sample = report.samples.at(0);
        const SurfaceId expected{InstanceId{0}, PrimitiveId{primitive}};
        if (!sample.surface || sample.surface->hit.surface.id != expected || sample.surface->hit.back_face ||
            std::abs(sample.surface->hit.view_depth - 4) > 1e-12 || !sample.lights.empty())
        {
            throw std::runtime_error(std::format("Incorrect primary hit at ({}, 16)", x));
        }
        check_rgb(image[16, x], amber);
        check_rgb(report.color, amber);
    }
    const auto miss = diagnose_pixel(request, {0, 0});
    if (miss.samples.front().surface)
    {
        throw std::runtime_error("Background ray unexpectedly hit a surface");
    }
    check_rgb(miss.color, {});
    check_rgb(image[0, 0], {});

    // A dark triangle in front must stop the ray even though the panel behind it emits light.
    auto preview = load_preview(request);
    const TriangleVertex a{{-1, -1, 1}}, b{{1, -1, 1}}, c{{0, 1, 1}};
    preview.scene.add_solid_mesh({Triangle{.vertices = {a, b, c}}}, {1, 1, 1});
    auto view = clip_to_bounds(preview.view.camera, world_bounds(preview.scene.instances()));
    view.ssaa = 2;
    const PreviewRenderer renderer{preview.scene, request.lighting, request.mode};
    const auto occluded = renderer.diagnose(view, {32, 16});
    if (occluded.samples.size() != 4)
    {
        throw std::runtime_error("Expected four primary rays");
    }
    for (const auto &sample : occluded.samples)
    {
        if (!sample.surface || sample.surface->hit.surface.id.instance != preview.scene.instances().back().id)
        {
            throw std::runtime_error("The nearest dark surface must occlude the emitter");
        }
    }
    check_rgb(occluded.color, {});
    check_rgb(renderer.render(view)[16, 32], {});
    view.depth = {3.5, 4.5};
    check_rgb(renderer.diagnose(view, {32, 16}).color, amber);
}
} // namespace

int main(int argc, char **argv)
try
{
    if (argc != 2)
    {
        return 1;
    }
    check_emitter(load_scene_config(argv[1]));
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
