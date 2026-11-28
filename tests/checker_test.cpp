import std;
import formlight.path;
import formlight.preview;
import formlight.scene_toml;

namespace
{
using namespace formlight;

void check_cells(CheckerTexture checker)
{
    checker.cell_size = 0.5;
    for (const auto axis : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}})
    {
        for (int index = -4; index <= 4; ++index)
        {
            const double face = index * checker.cell_size;
            const auto color = index % 2 == 0 ? checker.even : checker.odd;
            const auto previous = index % 2 == 0 ? checker.odd : checker.even;
            if (checker.sample(axis * face) != color || checker.sample(axis * std::nextafter(face, 1e5)) != color ||
                checker.sample(axis * std::nextafter(face, -1e5)) != previous)
            {
                throw std::runtime_error(std::format("Incorrect checker transition at cell {}", index));
            }
        }
    }
    if (checker.sample({-0.25, 0.75, 0.25}) != checker.even || checker.sample({-0.25, 0.75, -0.25}) != checker.odd)
    {
        throw std::runtime_error("Three-axis parity did not alternate at an edge or corner");
    }
    // (2^80 - 1) / 3 is an odd integer. The floor index cannot fit in an integer register.
    checker.cell_size = 3;
    if (checker.sample({0x1p80, 0, 0}) != checker.odd || checker.sample({-0x1p80, 0, 0}) != checker.even)
    {
        throw std::runtime_error("Large checker coordinate lost its parity");
    }
}

void check_material(SceneConfig config)
{
    auto &sphere = config.scene.spheres.at(0);
    sphere.albedo = {0.5, 1, 0.25};
    sphere.shape.displacement = {0, 0, 0.5};
    config.scene.environment = {1, 1, 1};
    config.lighting.reset();
    const auto preview = load_preview(prepare_render(config));
    const PathIntegrator integrator{preview.scene};
    for (const double time : {0.0, 1.0})
    {
        for (const double side : {-1.0, 1.0})
        {
            const Ray ray{sphere.shape.center_at(time) + Vec3{0, 0, 3 * side}, {0, 0, -side}, time};
            std::optional<PathHit> hit;
            const auto color =
                integrator.radiance(ray, {0, 6}, &hit, 0, AreaLightPattern::Center, {}, {.max_bounces = 1});
            // At the two poles on the Z axis, floor(+/-1.25 / 0.5) is either 2 or -3.
            const Vec3 tint = side > 0 ? Vec3{0.8, 0.72, 0.54} : Vec3{0.035, 0.22, 0.18};
            const Vec3 expected = component_product(sphere.albedo, tint);
            const auto matches = [&](LinearColor c)
            {
                return std::abs(c.r - expected.x) < 1e-7 && std::abs(c.g - expected.y) < 1e-7 &&
                       std::abs(c.b - expected.z) < 1e-7 && c.a == 1;
            };
            if (!hit || !matches(hit->material.base_color) || !matches(color))
            {
                throw std::runtime_error("Checker tint or moving reference pose differs at the path hit");
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
    const auto config = formlight::load_scene_config(argv[1]);
    check_cells(config.scene.spheres.at(0).checker.value());
    check_material(config);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
