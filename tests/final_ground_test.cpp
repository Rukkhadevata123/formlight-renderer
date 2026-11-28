import std;
import formlight.preview;
import formlight.scene_toml;

namespace
{
using namespace formlight;

void check_ground(const ScenePreview &preview)
{
    const auto instances = preview.scene.instances();
    if (instances.size() != 400 || preview.scene.mesh_count() != 1 || instances.front().triangles.size() != 12)
    {
        throw std::runtime_error("Expected 400 instances sharing one closed cube mesh");
    }
    const auto mesh = instances.front().triangles;
    for (const auto &triangle : mesh)
    {
        const auto center_to_face = triangle.vertices[0].position - Vec3{0.5, 0.5, 0.5};
        if (dot(triangle.geometric_normal(), center_to_face) <= 0)
        {
            throw std::runtime_error("Ground cube face winding points inward");
        }
    }
    const SurfaceBvh tree{instances, {}};
    for (std::size_t cell = 0; cell < instances.size(); ++cell)
    {
        const auto &instance = instances[cell];
        const auto box = world_bounds(instances.subspan(cell, 1));
        const double x = -1000 + 100 * static_cast<double>(cell / 20);
        const double z = -1000 + 100 * static_cast<double>(cell % 20);
        if (instance.triangles.data() != mesh.data() || box.low != Vec3{x, 0, z} || box.high.x != x + 100 ||
            box.high.z != z + 100 || !(box.high.y >= 1 && box.high.y < 101))
        {
            throw std::runtime_error(std::format("Incorrect footprint, height or mesh at cell {}", cell));
        }
        const Ray ray{{x + 35, 150, z + 65}, {0, -1, 0}};
        const auto hit = tree.intersect_surface(ray, {0, 200}).value();
        if (hit.surface.id.instance != instance.id || std::abs(hit.t - (150 - box.high.y)) > 1e-10)
        {
            throw std::runtime_error(std::format("Top-down ray missed the expected roof at cell {}", cell));
        }
    }
    const auto side = tree.intersect_surface({{-1010, 0.5, -950}, {1, 0, 0}}, {0, 20}).value();
    if (side.surface.id.instance != instances.front().id || std::abs(side.t - 10) > 1e-10 ||
        tree.occluded({{1001, 150, 0}, {0, -1, 0}}, {0, 200}))
    {
        throw std::runtime_error("Exterior wall or ray outside the grid differs from the expected footprint");
    }
    if (preview.bounds.low != Vec3{-1000, 0, -1000} || preview.bounds.high.x != 1000 || preview.bounds.high.z != 1000 ||
        std::abs(preview.bounds.high.y - 100.86514990142061) > 1e-10)
    {
        throw std::runtime_error("Seed 42 ground bounds differ from the reference grid");
    }
}

void check_seeds(SceneConfig config)
{
    const auto first = load_preview(prepare_render(config));
    check_ground(first);
    ++config.render.sampling.seed;
    const auto second = load_preview(prepare_render(config));
    const auto height = [](const MeshInstance &instance) { return instance.transform.to_world_point({0, 1, 0}).y; };
    if (!std::ranges::equal(first.scene.instances(), second.scene.instances(), std::ranges::equal_to{}, height, height))
    {
        throw std::runtime_error("Render seed changed the ground geometry");
    }
    ++config.scene.seed;
    const auto third = load_preview(prepare_render(config));
    if (std::abs(height(first.scene.instances().front()) - 20.295511080951812) > 1e-10 ||
        std::abs(height(third.scene.instances().front()) - 22.537643494623847) > 1e-10)
    {
        throw std::runtime_error("First ground box height differs for layout seeds 42 and 43");
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
    check_seeds(formlight::load_scene_config(argv[1]));
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
