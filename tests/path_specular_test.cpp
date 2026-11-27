import std;
import formlight.preview;
import formlight.scene_toml;

namespace
{
using namespace formlight;

void near(double actual, double expected, double tolerance, std::string_view label)
{
    if (!(std::abs(actual - expected) <= tolerance))
    {
        throw std::runtime_error(std::format("{}: expected {}, got {}", label, expected, actual));
    }
}

void check_optics()
{
    const Vec3 n{0, 1, 0}, incident{0.5, -std::sqrt(0.75), 0}; // 30 degrees from the normal.
    const auto reflected = mirror_direction(incident, n);
    near(reflected.x, incident.x, 1e-14, "mirror tangent");
    near(reflected.y, -incident.y, 1e-14, "mirror normal");
    const auto glass = sample_dielectric(incident, n, 1.0 / 1.5, 0.99);
    if (!glass.transmitted || glass.direction.y >= 0)
    {
        throw std::runtime_error("Expected transmission into glass");
    }
    near(glass.direction.x, 1.0 / 3, 1e-14, "Snell transmitted sine");
    near(dot(glass.direction, glass.direction), 1, 1e-14, "unit refracted direction");
    const auto air = sample_dielectric(glass.direction, n, 1.5, 0.99);
    near(air.direction.y, incident.y, 1e-14, "parallel slab exit");
    near(air.radiance_weight * glass.radiance_weight, 1, 1e-14, "parallel slab throughput");
    const auto tir = sample_dielectric({0.8, -0.6, 0}, n, 1.5, 0.999);
    if (tir.transmitted || tir.direction != Vec3{0.8, 0.6, 0} || tir.radiance_weight != 1 ||
        sample_dielectric({0, -1, 0}, n, 1.0 / 1.5, 0.0399).transmitted ||
        !sample_dielectric({0, -1, 0}, n, 1.0 / 1.5, 0.0401).transmitted)
    {
        throw std::runtime_error("Wrong Fresnel branch at normal incidence or above the critical angle");
    }
}

// Axial probes hit the sphere away from the triangle seams, directly below the ceiling lamp.
SampleDiagnostic probe(const ScenePreview &preview, Vec3 eye, bool up, std::uint32_t depth, BounceSampling method)
{
    const PinholeCamera camera{{eye, eye + Vec3{0, up ? 1.0 : -1.0, 0}, {0, 0, 1}, 0.5}, {1, 1}};
    PreviewView view{camera, {0.0001, 4}};
    view.path_sampling = {.samples = 8192, .max_bounces = depth, .seed = 7, .method = method};
    const PreviewRenderer renderer{preview.scene, {}, RenderMode::Path};
    return renderer.diagnose(view, {0, 0}).samples.at(0);
}

void check_cornell(SceneConfig &config, auto &sphere)
{
    const auto glass = load_preview(prepare_render(config));
    const Vec3 inside{-0.358, 0.34, 0.141};
    const auto exit = probe(glass, inside, true, 1, BounceSampling::LightMixture);
    if (!exit.surface.value().hit.back_face || exit.surface->material.emission.r != 0)
    {
        throw std::runtime_error("Expected a non-emissive glass exit surface");
    }
    // At normal incidence 96% transmit. Glass to air scales radiance by 1.5 squared.
    near(exit.color.r, 8 * 0.96 * 2.25, 0.15, "single glass exit");
    near(probe(glass, inside, true, 1, BounceSampling::Cosine).color.r, exit.color.r, 0, "delta sampler independence");

    sphere.ior = 1;
    const auto clear = load_preview(prepare_render(config));
    const Vec3 below{-0.358, -0.1, 0.141};
    near(probe(clear, below, true, 1, BounceSampling::LightMixture).color.r, 0, 0, "second interface before lamp");
    near(probe(clear, below, true, 2, BounceSampling::LightMixture).color.r, 8, 0, "two transmissions to lamp");

    sphere.type = PathSurface::Mirror;
    if constexpr (std::same_as<std::remove_cvref_t<decltype(sphere)>, SphereSettings>)
    {
        if (!std::holds_alternative<SphereCoordinates>(exit.surface->hit.surface.coordinates))
        {
            throw std::runtime_error("Expected an analytic sphere in the pixel diagnostic");
        }
        sphere.albedo = {0.8, 0.9, 1};
    }
    const auto mirror = load_preview(prepare_render(config));
    const auto lamp = probe(mirror, {-0.358, 1, 0.141}, false, 1, BounceSampling::LightMixture);
    near(lamp.color.r, 8 * 0.8, 1e-6, "red mirror reflectance");
    near(lamp.color.g, 8 * 0.9, 1e-6, "green mirror reflectance");
    near(lamp.color.b, 8, 0, "blue mirror reflectance");
    std::println("Cornell ceiling radiance through one glass exit: {:.5f}; mirror RGB: {:.2f}, {:.2f}, {:.2f}",
                 exit.color.r, lamp.color.r, lamp.color.g, lamp.color.b);
}
} // namespace

int main(int argc, char **argv)
try
{
    if (argc != 2)
    {
        return 1;
    }
    check_optics();
    auto config = load_scene_config(argv[1]);
    if (config.scene.spheres.empty())
    {
        check_cornell(config, config.scene.path_materials.at(0));
    }
    else
    {
        check_cornell(config, config.scene.spheres.at(0));
    }
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
