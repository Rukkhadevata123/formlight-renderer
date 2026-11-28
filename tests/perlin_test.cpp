import std;
import formlight.path;
import formlight.perlin;
import formlight.preview;
import formlight.scene_toml;

namespace
{
using namespace formlight;

void near(double actual, double expected, double tolerance, std::string_view label)
{
    if (!(std::abs(actual - expected) <= tolerance))
    {
        throw std::runtime_error(std::format("{}: expected {:.12g}, got {:.12g}", label, expected, actual));
    }
}

void check_field()
{
    const MarbleTexture marble{PerlinNoise{42}, 0.2};
    const Vec3 point{0.125, -1.75, 2.5};
    // Reference values from a separate eight-corner weighted sum with the same seed.
    near(marble.noise.noise(point), 0.17791378565932436, 1e-12, "gradient noise");
    near(marble.noise.turbulence(point), 0.3226669682825959, 1e-12, "seven octaves");
    near(marble.sample(point), 0.22386819955145826, 1e-12, "marble");
    near(marble.sample({220.123, 279.253, 379.871}), 0.4245291621040598, 1e-11, "nonzero high octaves");
    near(PerlinNoise{43}.noise(point), -0.16342050651750434, 1e-12, "second seed");
    near(marble.noise.noise(point + Vec3{-256, 512, -768}), marble.noise.noise(point), 0, "lattice period");
    near(marble.noise.noise({-3, 256, 7}), 0, 0, "lattice vertex");

    const Vec3 boundary{-1, 256, 0};
    constexpr double h = 1e-4;
    for (const Vec3 axis : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}})
    {
        // Cross one cell face away from its edges, including the periodic wrap at 256.
        const auto offset = Vec3{0.25, 0.375, 0.625};
        const auto face = boundary + offset - axis * dot(axis, offset);
        const double middle = marble.noise.noise(face);
        const double left_slope = (middle - marble.noise.noise(face - axis * h)) / h;
        const double right_slope = (marble.noise.noise(face + axis * h) - middle) / h;
        near(left_slope, right_slope, 2e-6, "gradient across a cell face");
    }
}

void check_material(SceneConfig config)
{
    auto &sphere = config.scene.spheres.at(0);
    sphere.shape = {{0.125, -1.75, 0.5}, 2, {3, 0, -4}};
    sphere.albedo = {1, 0.5, 0.25};
    config.scene.environment = {1, 1, 1};
    config.lighting.reset();
    const auto preview = load_preview(prepare_render(config));
    const PathIntegrator integrator{preview.scene};
    for (double time : {0.0, 0.5, 1.0})
    {
        const Ray ray{sphere.shape.center_at(time) + Vec3{0, 0, 4}, {0, 0, -1}, time};
        for (std::uint64_t seed : {7u, 91u})
        {
            std::optional<PathHit> hit;
            const auto color = integrator.radiance(ray, {0, 8}, &hit, 0, AreaLightPattern::Center, {},
                                                   {.max_bounces = 1, .seed = seed});
            const auto base = hit.value().material.base_color;
            near(base.r, 0.22386819955145826, 1e-7, "moving reference-pose marble");
            near(color.r, base.r, 0, "diffuse bounce");
            near(color.g, base.r * 0.5, 0, "green albedo");
            near(color.b, base.r * 0.25, 0, "blue albedo");
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
    check_field();
    check_material(formlight::load_scene_config(argv[1]));
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
