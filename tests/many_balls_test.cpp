import std;
import formlight.preview;
import formlight.scene_toml;

namespace
{
using namespace formlight;

bool same_sphere(const SphereSettings &a, const SphereSettings &b)
{
    return a.shape.center == b.shape.center && a.shape.radius == b.shape.radius && a.type == b.type &&
           a.albedo == b.albedo && a.ior == b.ior && a.fuzz == b.fuzz;
}

void check_layout(SceneConfig config)
{
    const auto original = prepare_render(config);
    const auto &spheres = original.spheres;
    if (spheres.size() != 484 || spheres.front().shape.center != Vec3{0, -1000, 0} ||
        spheres.front().shape.radius != 1000)
    {
        throw std::runtime_error("Unexpected ground or sphere count for scene seed 42");
    }
    const auto small = std::span{spheres}.subspan(1, spheres.size() - 4);
    const auto first = small.front().shape.center;
    if (std::abs(first.x + 10.684928213906886) > 1e-12 || std::abs(first.z + 10.883758596207397) > 1e-12)
    {
        throw std::runtime_error("First small sphere moved for scene seed 42");
    }
    std::array<unsigned, 4> counts{};
    for (const auto &ball : small)
    {
        const auto c = ball.shape.center;
        const Vec3 exclusion = c - Vec3{4, 0.2, 0};
        if (ball.shape.radius != 0.2 || c.y != 0.2 || dot(exclusion, exclusion) <= 0.81)
        {
            throw std::runtime_error("Small sphere outside the authored layout");
        }
        ++counts.at(std::to_underlying(ball.type));
        if (ball.type == PathSurface::Metal && !(ball.fuzz > 0 && ball.fuzz < 0.5))
        {
            throw std::runtime_error("Unexpected small metal sphere fuzz for scene seed 42");
        }
    }
    if (counts != std::array<unsigned, 4>{382, 0, 25, 73})
    {
        throw std::runtime_error("Small sphere material counts changed for scene seed 42");
    }

    ++config.render.sampling.seed;
    const auto another_render = prepare_render(config);
    if (!std::ranges::equal(spheres, another_render.spheres, same_sphere))
    {
        throw std::runtime_error("Render seed changed the layout or materials");
    }
    ++config.scene.seed;
    const auto another_scene = prepare_render(config);
    if (another_scene.spheres[1].shape.center == spheres[1].shape.center ||
        !std::ranges::equal(std::span{spheres}.last(3), std::span{another_scene.spheres}.last(3), same_sphere))
    {
        throw std::runtime_error("Scene seed must move the small balls and preserve the three large ones");
    }

    const auto preview = load_preview(original);
    const PreviewRenderer renderer{preview.scene, original.lighting, original.mode};
    for (const auto [x, material] : {std::pair{0, PathSurface::Dielectric}, std::pair{-4, PathSurface::Diffuse},
                                     std::pair{4, PathSurface::Mirror}})
    {
        const Vec3 center{static_cast<double>(x), 1, 0};
        const PinholeCamera camera{{center + Vec3{0, 3, 0}, center, {0, 0, 1}, 0.5}, {1, 1}};
        const auto probe = renderer.diagnose({camera, {0.001, 10}}, {0, 0});
        const auto &hit = probe.samples.front().surface.value();
        const auto index = std::get<SphereCoordinates>(hit.hit.surface.coordinates).index;
        if (hit.hit.view_depth != 2 ||
            preview.scene.material(preview.scene.spheres()[index].material_index).path_surface != material)
        {
            throw std::runtime_error("Large sphere depth or material differs at its top pole");
        }
    }
}

void check_environment()
{
    SceneConfig config;
    config.scene.environment = {0.75, 0.5, 1};
    config.scene.spheres = {{.albedo = {0.5, 0.25, 0.75}}};
    config.camera = {.auto_frame = false, .vertical_fov_degrees = 60, .position = {0, 0, 3}};
    config.render.mode = RenderMode::Path;
    config.render.extent = {3, 1};
    config.render.max_bounces = 1;
    for (const auto type : {PathSurface::Diffuse, PathSurface::Mirror})
    {
        config.scene.spheres.front().type = type;
        const auto request = prepare_render(config);
        const auto frame = render_scene(request);
        const auto sky = frame[0, 0], reflected = frame[0, 1];
        if (Vec3{sky.r, sky.g, sky.b} != config.scene.environment ||
            Vec3{reflected.r, reflected.g, reflected.b} != Vec3{0.375, 0.125, 0.75})
        {
            throw std::runtime_error("Expected sky on a primary miss and albedo times sky on a secondary miss");
        }
    }
}
} // namespace

int main(int argc, char **argv)
try
{
    if (argc != 2)
        return 1;
    check_layout(load_scene_config(argv[1]));
    check_environment();
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
