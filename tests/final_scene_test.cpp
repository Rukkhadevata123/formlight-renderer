import std;
import formlight.path;
import formlight.preview;
import formlight.scene_toml;

namespace
{
using namespace formlight;

void check_composition(const ScenePreview &preview)
{
    const auto spheres = preview.scene.spheres();
    const auto meshes = preview.scene.instances();
    if (meshes.size() != 401 || spheres.size() != 1006 || preview.scene.media.size() != 2)
    {
        throw std::runtime_error("Final scene is missing geometry or media");
    }
    const auto subjects = spheres.last(6);
    const auto material = [&](std::size_t i) -> const GltfMaterial &
    { return preview.scene.material(subjects[i].material_index); };
    const auto &blue = preview.scene.media[0];
    const auto &fog = preview.scene.media[1];
    if (subjects[0].shape.center_at(1) != Vec3{430, 400, 200} || material(1).path_surface != PathSurface::Dielectric ||
        material(2).path_surface != PathSurface::Metal || material(2).fuzz != 1 ||
        material(3).path_surface != PathSurface::Dielectric || subjects[3].shape.center != blue.boundary.center ||
        subjects[3].shape.radius != blue.boundary.radius || blue.scattering_albedo != Vec3{0.2, 0.4, 0.9} ||
        blue.extinction != 0.2 || fog.boundary.radius != 5000 || fog.extinction != 0.0001 ||
        !material(4).base_color_texture || !material(5).marble_texture)
    {
        throw std::runtime_error("Final scene material, motion or medium differs from the reference");
    }

    // Geometric visibility at mid-shutter is independent of the number of light or medium samples.
    const SurfaceBvh bvh{meshes, spheres};
    std::array<bool, 6> visible{};
    bool ground = false, cluster = false, lamp = false;
    const auto &camera = preview.view.camera;
    const auto extent = camera.extent();
    for (int y = 0; y < extent.height; y += 2)
    {
        for (int x = 0; x < extent.width; x += 2)
        {
            auto ray = camera.ray_for_sample(pixel_center(x, y));
            ray.time = 0.5;
            const auto hit = bvh.intersect_surface(ray, {0, 10000});
            if (!hit)
            {
                continue;
            }
            if (const auto *sphere = std::get_if<SphereCoordinates>(&hit->surface.coordinates))
            {
                if (sphere->index < 1000)
                {
                    cluster = true;
                }
                else
                {
                    visible.at(sphere->index - 1000) = true;
                }
            }
            else if (hit->surface.id.instance == meshes.back().id)
            {
                lamp = true;
            }
            else
            {
                ground = true;
            }
        }
    }
    if (!ground || !cluster || !lamp || std::ranges::contains(visible, false))
    {
        throw std::runtime_error("Reference framing lost a subject, the lamp or the ground");
    }
}

void check_lamp(ScenePreview &preview)
{
    const Ray front{{250, 553, 250}, {0, 1, 0}}, back{{250, 555, 250}, {0, -1, 0}};
    const Ray horizontal{{200, 553, 200}, {1, 0, 0}};
    const PathSampling single_scatter{.samples = 65536, .max_bounces = 1, .seed = 7};
    constexpr auto pattern = AreaLightPattern::ShiftedHalton;
    {
        const PathIntegrator foggy{preview.scene};
        // Nothing lies on the 100-unit primary segment. Light must scatter in from the emissive triangles.
        const double scattered = foggy.radiance(horizontal, {0, 100}, nullptr, 0, pattern, {}, single_scatter).r;
        const double upper = 7 * -std::expm1(-0.0001 * 100);
        if (!(scattered > 0.01 && scattered < upper))
        {
            throw std::runtime_error("Expected single-scattered top light in the global fog");
        }
        std::println("Fog probe, one scatter: {:.5f}", scattered);
    }
    preview.scene.media.clear();
    const PathIntegrator clear{preview.scene};
    if (clear.radiance(front, {0, 2}).r != 7 || clear.radiance(back, {0, 2}).r != 0 ||
        clear.radiance(horizontal, {0, 100}, nullptr, 0, pattern, {}, single_scatter).r != 0)
    {
        throw std::runtime_error("One-sided lamp or clear-air control differs from expected radiance");
    }
    const Vec3 center{260, 150, 45};
    const Vec3 normal = unit_vector(Vec3{273, 554, 279.5} - center, "Invalid lamp direction");
    const Ray glass{center + normal * 55, normal * -1};
    const auto reflected = clear.radiance(glass, {0, 20}, nullptr, 0, pattern, {}, single_scatter).r;
    // At normal incidence 4% reflect into the lamp. Transmission reaches the unlit far interface at depth one.
    std::println("Glass probe, one bounce: {:.5f}, expected 0.28", reflected);
    if (std::abs(reflected - 0.28) > 0.026)
    {
        throw std::runtime_error("Glass did not reflect the emissive mesh at its expected Fresnel weight");
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
    const auto config = formlight::load_scene_config(argv[1]);
    const auto request = formlight::prepare_render(config);
    if (request.lighting || request.environment != formlight::Vec3{} || request.shutter.open != 0 ||
        request.shutter.close != 1)
    {
        throw std::runtime_error("Final scene must use the emissive mesh and full shutter");
    }
    auto preview = formlight::load_preview(request);
    check_composition(preview);
    check_lamp(preview);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
