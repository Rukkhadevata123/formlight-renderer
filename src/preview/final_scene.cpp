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
} // namespace

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
} // namespace formlight
