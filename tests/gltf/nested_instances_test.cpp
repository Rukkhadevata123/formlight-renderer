import std;
import formlight.preview;
import formlight.ray_query;

namespace
{
using namespace formlight;

struct ExpectedInstance
{
    InstanceId id;
    std::array<Vec3, 3> world_vertices;
};

constexpr ExpectedInstance expected[]{
    {InstanceId{1}, {{{1.5, -0.5, 0}, {2.5, -0.5, 0}, {2, 0.5, 0}}}},
    {InstanceId{3}, {{{-2.7, 2, 1.8}, {-2.7, 0, 1.8}, {-3.3, 1, 2.2}}}},
    {InstanceId{5}, {{{0.5, -2.5, 1}, {0.5, -0.5, 1}, {-0.5, -1.5, 1}}}},
};

void check_instances(const GltfScene &scene)
{
    if (scene.mesh_count() != 1 || scene.instances().size() != std::size(expected))
    {
        throw std::runtime_error("Expected three instances of one mesh");
    }
    const auto shared = scene.instances().front().triangles;
    for (const auto &[id, vertices] : expected)
    {
        const auto &instance = scene.instance(id);
        if (instance.id != id || instance.triangles.size() != 1 || instance.triangles.data() != shared.data())
        {
            throw std::runtime_error(
                std::format("Node {} did not reference the shared triangle", std::to_underlying(id)));
        }
        for (std::size_t i = 0; i < vertices.size(); ++i)
        {
            const auto actual = instance.transform.to_world_point(shared.front().vertices[i].position);
            const auto error = abs_components(actual - vertices[i]);
            if (!(std::max({error.x, error.y, error.z}) < 1e-6))
            {
                throw std::runtime_error(std::format("Node {} vertex {}: unexpected world position ({}, {}, {})",
                                                     std::to_underlying(id), i, actual.x, actual.y, actual.z));
            }
        }
    }
}

void check_picks(const GltfScene &scene)
{
    const PinholeCamera camera{{{0, 0, 9}, {0, 0, 0}, {0, 1, 0}, std::numbers::pi / 3}, {256, 192}};
    const auto view = clip_to_bounds(camera, world_bounds(scene.instances()));
    const PreviewRenderer renderer{scene};
    const TriangleBvh bvh{scene.instances()};
    for (const auto &[id, vertices] : expected)
    {
        // Choose the pixel from the analytical coordinates, independently of the imported transform.
        const auto center = (vertices[0] + vertices[1] + vertices[2]) / 3;
        const auto projected = camera.project(center).value().image;
        const PixelCoord pixel{static_cast<int>(projected.x), static_cast<int>(projected.y)};
        const auto pick = renderer.diagnose(view, pixel);
        const auto &sample = pick.samples.at(0);
        const auto ray = camera.ray_for_sample(pixel_center(pixel.x, pixel.y));
        const auto hit = bvh.intersect(ray, {0, std::numeric_limits<double>::infinity()});
        const SurfaceId target{id, PrimitiveId{0}};
        if (!sample.surface || sample.surface->hit.surface.id != target || !hit || hit->surface.id != target)
        {
            throw std::runtime_error(
                std::format("Expected node {} primitive 0 at ({}, {})", std::to_underlying(id), pixel.x, pixel.y));
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
    const auto scene = formlight::GltfScene::load(argv[1], formlight::GltfContent::Materials);
    if (!scene)
    {
        throw std::runtime_error(scene.error());
    }
    check_instances(*scene);
    check_picks(*scene);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
