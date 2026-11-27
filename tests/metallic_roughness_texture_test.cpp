import std;
import formlight.gltf;

using namespace formlight;

int main(int argc, char **argv)
try
{
    if (argc != 2)
    {
        return 1;
    }
    const auto scene = GltfScene::load_glb(argv[1], GltfContent::Materials);
    if (!scene)
    {
        throw std::runtime_error(scene.error());
    }
    const auto &face = scene->instances().front().triangles.front();
    const auto check = [&](double u, Vec3 expected_color, double green, double blue)
    {
        const auto surface = scene->surface_material(face, {1 - u, u / 2, u / 2});
        const auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };
        if (!near(surface.roughness, 0.5 * green / 255) || !near(surface.metallic, 0.5 * blue / 255))
        {
            throw std::runtime_error(
                std::format("At u={}, roughness={}, metallic={}", u, surface.roughness, surface.metallic));
        }
        const auto color = surface.base_color;
        if (!near(color.r, expected_color.x) || !near(color.g, expected_color.y) || !near(color.b, expected_color.z))
        {
            throw std::runtime_error(std::format("At u={}, base color=({}, {}, {})", u, color.r, color.g, color.b));
        }
    };

    // Both slots reference the same two texels. Filter color after sRGB decoding and data in linear space.
    const Vec3 left{0.01444384, 0.05126946, 0.52711513};
    const Vec3 right{0.74540421, 0.35153260, 0.02955683};
    check(0.25, left, 64, 192);
    check(0.75, right, 160, 48);
    check(0.5, (left + right) * 0.5, 112, 120);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
