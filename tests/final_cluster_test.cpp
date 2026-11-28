import std;
import formlight.preview;
import formlight.sampling;
import formlight.scene_toml;

namespace
{
using namespace formlight;

void check_point(Vec3 actual, Vec3 expected, std::string_view label)
{
    const auto error = actual - expected;
    if (!(std::hypot(error.x, error.y, error.z) < 1e-9))
    {
        throw std::runtime_error(std::format("{} differs: ({}, {}, {})", label, actual.x, actual.y, actual.z));
    }
}

void check_cluster(const ScenePreview &preview)
{
    const auto spheres = preview.scene.spheres();
    if (spheres.size() != 1000)
    {
        throw std::runtime_error("Expected one thousand cluster spheres");
    }
    const auto slot = spheres.front().material_index;
    const auto &material = preview.scene.material(slot);
    const auto color = material.base_color_factor;
    if (material.path_surface != PathSurface::Diffuse || material.metallic_factor != 0 || color.r != 0.73f ||
        color.g != color.r || color.b != color.r)
    {
        throw std::runtime_error("Expected neutral diffuse cluster material");
    }

    Bounds3 cluster = Bounds3::empty();
    const double angle = std::numbers::pi / 12;
    for (std::size_t i = 0; i < spheres.size(); ++i)
    {
        const auto &sphere = spheres[i];
        if (sphere.material_index != slot || sphere.shape.radius != 10 || sphere.shape.displacement != Vec3{})
        {
            throw std::runtime_error("Cluster material, radius or motion changed");
        }
        // Undo translation and rotation separately and recover the three original samples.
        const Vec3 p = sphere.shape.center - Vec3{-100, 270, 395};
        const Vec3 local{p.x * std::cos(angle) - p.z * std::sin(angle), p.y,
                         p.x * std::sin(angle) + p.z * std::cos(angle)};
        PathSampler rng{42, i, 1};
        check_point(local, {165 * rng.uniform(), 165 * rng.uniform(), 165 * rng.uniform()}, "local center");
        const auto box = sphere.shape.bounds();
        cluster.include(box.low);
        cluster.include(box.high);
    }

    // Independent reference bounds of the transformed centers, expanded by the sphere radius.
    check_point(cluster.low, {-108.990244100449, 260.140869991875, 343.876374413028}, "cluster minimum");
    check_point(cluster.high, {110.154450395073, 444.959524416511, 562.822095969674}, "cluster maximum");
    check_point(preview.bounds.low, {-1000, 0, -1000}, "combined minimum");
    check_point(preview.bounds.high, {1000, cluster.high.y, 1000}, "combined maximum");

    // Nearest positive root along +Z, independently computed over the seed-42 centers.
    const SurfaceBvh tree{preview.scene.instances(), spheres};
    const Ray ray{{0, 350, 200}, {0, 0, 1}};
    constexpr double depth = 176.472147309244;
    const auto hit = tree.intersect_surface(ray, {0, 400}).value();
    if (std::get<SphereCoordinates>(hit.surface.coordinates).index != 171 ||
        hit.surface.id.instance != spheres[171].id || std::abs(hit.t - depth) > 1e-9 ||
        tree.occluded(ray, {0, depth - 1e-6}) || !tree.occluded(ray, {0, depth + 1e-6}) ||
        tree.occluded({{0, 450, 200}, {0, 0, 1}}, {0, 400}))
    {
        throw std::runtime_error("Mixed BVH failed cluster nearest-hit, segment or miss query");
    }
}
} // namespace

int main(int argc, char **argv)
try
{
    using namespace formlight;
    if (argc != 2)
    {
        return 1;
    }
    auto config = load_scene_config(argv[1]);
    const auto preview = load_preview(prepare_render(config));
    check_cluster(preview);
    ++config.render.sampling.seed;
    const auto rerender = load_preview(prepare_render(config));
    const auto center = [](const SphereInstance &sphere) { return sphere.shape.center; };
    if (!std::ranges::equal(preview.scene.spheres(), rerender.scene.spheres(), std::ranges::equal_to{}, center, center))
    {
        throw std::runtime_error("Render seed changed the cluster geometry");
    }
    ++config.scene.seed;
    const auto changed = load_preview(prepare_render(config));
    check_point(changed.scene.spheres().front().shape.center, {38.054294159454, 274.851694587745, 462.434459673987},
                "seed 43 first center");
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
