import std;
import formlight.ray_query;

namespace
{
using namespace formlight;

void check_sphere()
{
    const Sphere sphere{{0, 0, 3}, 1};
    const Ray axial{{}, {0, 0, 2}};
    const auto check = [&](Ray ray, RayInterval interval, double t, Vec3 normal)
    {
        const auto hit = intersect_sphere(ray, sphere, interval);
        if (!hit || hit->t != t || hit->outward_normal != normal)
        {
            throw std::runtime_error(std::format("Sphere root or outward normal differs at expected t={}", t));
        }
    };
    check(axial, {0, 3}, 1, {0, 0, -1});
    check(axial, {1, 3}, 2, {0, 0, 1});
    check({sphere.center, {0, 2, 0}}, {0, 1}, 0.5, {0, 1, 0});
    check({{1, 0, 0}, {0, 0, 1}}, {0, 4}, 3, {1, 0, 0});
    check({{0, 0, -1e9}, {0, 0, 1}}, {0, 2e9}, 1e9 + 2, {0, 0, -1});
    const auto oblique = intersect_sphere({{0.6, 0, 0}, {0, 0, 1}}, sphere, {0, 4}).value();
    const auto error = oblique.outward_normal - Vec3{0.6, 0, -0.8};
    if (std::abs(oblique.t - 2.2) > 1e-14 || std::hypot(error.x, error.y, error.z) > 1e-14)
    {
        throw std::runtime_error("Off-axis sphere depth or normal differs from the 3-4-5 triangle");
    }
    if (intersect_sphere(axial, sphere, {1, 2}) ||
        intersect_sphere({{std::nextafter(1.0, 2.0), 0, 0}, {0, 0, 1}}, sphere, {0, 4}) ||
        intersect_sphere({{}, {}}, sphere, {0, 4}))
    {
        throw std::runtime_error("Open interval, tangent miss or zero direction accepted");
    }
    const SurfaceRayOrigin surface{sphere, {0, 0, -1}};
    if (intersect_sphere(surface.along({0, 0, -1}), sphere, {0, 4}) ||
        intersect_sphere(surface.along({0, 0, 1}), sphere, {0, 4}).value().t < 1.99)
    {
        throw std::runtime_error("Offset secondary ray hit its starting interface");
    }
}

void check_mixed_tree()
{
    const Triangle sheet{
        .vertices = {TriangleVertex{{-1, -1, 5}}, TriangleVertex{{0, 1, 5}}, TriangleVertex{{1, -1, 5}}}};
    const std::array meshes{MeshInstance{InstanceId{7}, std::span{&sheet, 1}}};
    const std::array spheres{SphereInstance{InstanceId{8}, {{0, 0, 3}, 1}, 0, false}};
    const SurfaceBvh bvh{meshes, spheres};
    const Ray ray{{}, {0, 0, 1}};
    RayQueryStats stats;
    const auto front = bvh.intersect_surface(ray, {0, 6}, &stats).value();
    if (front.surface.id.instance != InstanceId{8} || front.t != 2 ||
        std::get<SphereCoordinates>(front.surface.coordinates).outward_normal != Vec3{0, 0, -1} ||
        stats.sphere_tests != 1 || stats.triangle_tests != 1)
    {
        throw std::runtime_error("Nearest analytic sphere did not win the mixed BVH query");
    }
    const auto behind = bvh.intersect_surface(ray, {4, 6}).value();
    if (behind.surface.triangle().id.instance != InstanceId{7} || behind.t != 5 || bvh.occluded(ray, {0, 2}) ||
        !bvh.occluded(ray, {0, 2.5}))
    {
        throw std::runtime_error("Mixed BVH did not respect the shadow endpoint or the sheet behind the sphere");
    }
}
} // namespace

int main()
try
{
    check_sphere();
    check_mixed_tree();
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
