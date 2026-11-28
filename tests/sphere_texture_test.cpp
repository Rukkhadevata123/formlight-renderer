import std;
import formlight.path;
import formlight.preview;

namespace
{
using namespace formlight;

void check_samples(const std::filesystem::path &texture)
{
    auto scene = GltfScene::empty_scene();
    scene.environment = {1, 1, 1};
    const SphereSettings sphere{.shape = {{3, 0, 4}, 2, {0, 0, 4}}, .albedo = {0.5, 1, 1}, .albedo_texture = texture};
    scene.add_spheres(std::span{&sphere, 1});
    const PathIntegrator integrator{scene};

    // The PNG encodes red by column (0,64,128,255), green by row (0,128,255), and constant blue (64).
    constexpr double dark = 0.05126946, mid = 0.21586050;
    const auto check = [&](Vec3 normal, double red, double green, bool inside = false)
    {
        const Vec3 center = sphere.shape.center_at(0.75);
        const Ray ray = inside ? Ray{center, normal, 0.75} : Ray{center + normal * 4, normal * -1, 0.75};
        std::optional<PathHit> hit;
        const auto outgoing =
            integrator.radiance(ray, {0, 10}, &hit, 0, AreaLightPattern::Center, {}, {.max_bounces = 1});
        const auto &base = hit.value().material.base_color;
        const auto matches = [&](LinearColor c)
        { return std::abs(c.r - 0.5 * red) < 1e-6 && std::abs(c.g - green) < 1e-6 && std::abs(c.b - dark) < 1e-6; };
        if (hit->back_face != inside || !matches(base) || (!inside && !matches(outgoing)))
        {
            throw std::runtime_error(std::format("Wrong textured hit along normal ({}, {}, {}): ({}, {}, {})", normal.x,
                                                 normal.y, normal.z, base.r, base.g, base.b));
        }
    };
    check({1, 0, 0}, (dark + mid) / 2, mid);
    check({0, 0, 1}, dark / 2, mid);
    check({0, 0, -1}, (mid + 1) / 2, mid);
    check({-1, 0, 0}, 0.5, mid);
    check({-1, 0, 1e-9}, 0.5, mid);
    check({-1, 0, -1e-9}, 0.5, mid);
    check({0, 1, 0}, (dark + mid) / 2, 0);
    check({0, -1, 0}, (dark + mid) / 2, 1);
    check({0, 0, 1}, dark / 2, mid, true);
}

void check_texture_batch(const std::filesystem::path &texture)
{
    auto scene = GltfScene::empty_scene();
    const SphereSettings sphere{.albedo_texture = texture};
    std::array batch{sphere, sphere};
    batch.back().albedo_texture += ".missing";
    try
    {
        scene.add_spheres(batch);
        throw std::logic_error("Missing sphere texture was accepted");
    }
    catch (const std::runtime_error &)
    {
        if (!scene.spheres().empty())
        {
            throw std::runtime_error("Failed texture batch left sphere instances in the scene");
        }
    }
    batch.back() = sphere;
    scene.add_spheres(batch);
    scene.add_spheres(std::span{&sphere, 1});
    const auto image = scene.material(scene.spheres().front().material_index).base_color_texture.value().image;
    for (const auto &instance : scene.spheres())
    {
        if (scene.material(instance.material_index).base_color_texture.value().image != image)
        {
            throw std::runtime_error("Repeated sphere texture did not share the decoded image");
        }
    }
    try
    {
        scene.check_output_path(texture);
    }
    catch (const std::invalid_argument &)
    {
        return;
    }
    throw std::runtime_error("Sphere texture not recognized as a scene input");
}
} // namespace

int main(int argc, char **argv)
try
{
    if (argc != 2)
    {
        return 1;
    }
    check_samples(argv[1]);
    check_texture_batch(argv[1]);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
