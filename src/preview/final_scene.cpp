module formlight.preview;

import std;
import formlight.sampling;

namespace formlight
{
namespace
{
std::vector<Triangle> unit_cube()
{
    std::vector<Triangle> mesh;
    mesh.reserve(12);
    const auto face = [&](Vec3 origin, Vec3 u, Vec3 v)
    {
        const TriangleVertex a{origin}, b{origin + u}, c{origin + u + v}, d{origin + v};
        mesh.push_back({.vertices = {a, b, c}});
        mesh.push_back({.vertices = {a, c, d}});
    };
    constexpr Vec3 x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};
    face({}, z, y);
    face(x, y, z);
    face({}, x, z);
    face(y, z, x);
    face({}, y, x);
    face(z, x, y);
    return mesh;
}

void add_final_scene_ground(GltfScene &scene, std::uint64_t scene_seed)
{
    constexpr int side = 20;
    constexpr double width = 100;
    std::vector<AffineTransform> transforms;
    transforms.reserve(side * side);
    for (int x = 0; x < side; ++x)
    {
        for (int z = 0; z < side; ++z)
        {
            const auto cell = static_cast<std::uint64_t>(x * side + z);
            PathSampler rng{scene_seed, cell, 0};
            const double height = 1 + width * rng.uniform();
            const Vec3 corner{(x - side / 2) * width, 0, (z - side / 2) * width};
            transforms.emplace_back(Basis3{{width, 0, 0}, {0, height, 0}, {0, 0, width}}, corner);
        }
    }
    scene.add_solid_mesh(unit_cube(), {0.48f, 0.83f, 0.53f, 1}, std::move(transforms));
}
void add_sphere_cluster(GltfScene &scene, std::uint64_t scene_seed)
{
    constexpr double angle = 15 * std::numbers::pi / 180;
    const double c = std::cos(angle), s = std::sin(angle);
    const AffineTransform placement{Basis3{{c, 0, -s}, {0, 1, 0}, {s, 0, c}}, {-100, 270, 395}};

    std::vector<Sphere> cluster;
    constexpr unsigned count = 1000;
    cluster.reserve(count);
    for (unsigned i = 0; i < count; ++i)
    {
        // Stream zero belongs to the ground grid. Each sphere has its own stream in the cluster.
        PathSampler rng{scene_seed, i, 1};
        const Vec3 local{165 * rng.uniform(), 165 * rng.uniform(), 165 * rng.uniform()};
        cluster.push_back({placement.to_world_point(local), 10});
    }
    scene.add_diffuse_spheres(cluster, {0.73, 0.73, 0.73});
}
} // namespace

void add_final_scene_geometry(GltfScene &scene, std::uint64_t scene_seed)
{
    add_final_scene_ground(scene, scene_seed);
    add_sphere_cluster(scene, scene_seed);
    scene.add_emissive_quad({123, 554, 147}, {300, 0, 0}, {0, 0, 265}, {7, 7, 7, 1});
}
} // namespace formlight
