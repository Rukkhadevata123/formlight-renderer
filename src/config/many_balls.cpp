module formlight.scene_config;

import std;
import formlight.sampling;

namespace formlight
{
void append_many_balls(std::vector<SphereSettings> &spheres, std::uint64_t seed, double max_fuzz)
{
    constexpr int half_grid = 11, side = 2 * half_grid;
    spheres.reserve(spheres.size() + side * side + 4);
    spheres.push_back({.shape = {{0, -1000, 0}, 1000}, .albedo = {0.5, 0.5, 0.5}});

    for (int x = -half_grid; x < half_grid; ++x)
    {
        for (int z = -half_grid; z < half_grid; ++z)
        {
            // Each cell owns a stream so changing one material's draws cannot move the other spheres.
            const auto cell = static_cast<std::uint64_t>((x + half_grid) * side + z + half_grid);
            PathSampler rng{seed, cell, 0};
            const double choice = rng.uniform();
            const Vec3 center{x + 0.9 * rng.uniform(), 0.2, z + 0.9 * rng.uniform()};
            const Vec3 delta = center - Vec3{4, 0.2, 0};
            if (dot(delta, delta) <= 0.9 * 0.9)
            {
                continue;
            }
            SphereSettings ball{.shape = {center, 0.2}};
            if (choice < 0.8)
            {
                const Vec3 a{rng.uniform(), rng.uniform(), rng.uniform()};
                const Vec3 b{rng.uniform(), rng.uniform(), rng.uniform()};
                ball.albedo = component_product(a, b);
            }
            else if (choice < 0.95)
            {
                ball.type = PathSurface::Metal;
                ball.albedo = {0.5 + 0.5 * rng.uniform(), 0.5 + 0.5 * rng.uniform(), 0.5 + 0.5 * rng.uniform()};
                ball.fuzz = max_fuzz * rng.uniform();
            }
            else
            {
                ball.type = PathSurface::Dielectric;
            }
            spheres.push_back(ball);
        }
    }
    spheres.push_back({.shape = {{0, 1, 0}, 1}, .type = PathSurface::Dielectric});
    spheres.push_back({.shape = {{-4, 1, 0}, 1}, .albedo = {0.4, 0.2, 0.1}});
    spheres.push_back({.shape = {{4, 1, 0}, 1}, .albedo = {0.7, 0.6, 0.5}, .type = PathSurface::Mirror});
}
} // namespace formlight
