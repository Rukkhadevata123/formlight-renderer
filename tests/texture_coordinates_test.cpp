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
    const Vec3 left{0.01444384, 0.05126946, 0.52711513};
    const Vec3 right{0.74540421, 0.35153260, 0.02955683};
    for (const double u : {0.375, 0.625})
    {
        const auto surface = scene->surface_material(face, {1 - u, u / 2, u / 2});
        const auto check = [u](std::string_view channel, double actual, double expected)
        {
            if (!(std::abs(actual - expected) < 1e-6))
            {
                throw std::runtime_error(std::format("{} at u={}: expected {}, got {}", channel, u, expected, actual));
            }
        };
        // Linear filtering between texel centers at 0.25 and 0.75. UV1 mirrors U.
        const double t = (u - 0.25) * 2;
        const auto base = left * t + right * (1 - t);
        const auto emission = left * (1 - t) + right * t;
        check("base.r", surface.base_color.r, base.x);
        check("base.g", surface.base_color.g, base.y);
        check("base.b", surface.base_color.b, base.z);
        check("emission.r", surface.emission.r, emission.x);
        check("emission.g", surface.emission.g, emission.y);
        check("emission.b", surface.emission.b, emission.z);

        // The data bindings use nearest filtering, with UV0 for G/B and UV1 for R.
        check("roughness", surface.roughness, 0.5 * (u < 0.5 ? 64 : 160) / 255);
        check("metallic", surface.metallic, 0.5 * (u < 0.5 ? 192 : 48) / 255);
        check("occlusion", surface.occlusion, 0.75 + 0.25 * (u < 0.5 ? 224 : 32) / 255);
    }
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
