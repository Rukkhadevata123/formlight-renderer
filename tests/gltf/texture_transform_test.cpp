import std;
import formlight.gltf;

namespace
{
using namespace formlight;

void compare(std::string_view channel, double actual, double baked)
{
    // The reference model stores its pre-transformed UVs as 32-bit floats.
    if (!(std::abs(actual - baked) < 2e-6))
    {
        throw std::runtime_error(std::format("{}: transformed {}, baked {}", channel, actual, baked));
    }
}

void compare(std::string_view channel, LinearColor actual, LinearColor baked)
{
    compare(channel, actual.r, baked.r);
    compare(channel, actual.g, baked.g);
    compare(channel, actual.b, baked.b);
}

void check_bindings(const GltfScene &scene)
{
    const auto &transformed = scene.instance(InstanceId{0}), &baked = scene.instance(InstanceId{1});
    const auto &face = transformed.triangles.front(), &reference = baked.triangles.front();
    const auto covered = scene.solid_filter();
    int holes = 0, solid = 0;
    // Exercise magnification, fractional mip levels in either screen direction, and the final mip.
    const std::array footprints{WeightGradients{}, WeightGradients{{0.27, -0.05}, {0, 0}},
                                WeightGradients{{0, 0}, {-0.11, 0.36}}, WeightGradients{{4, 0}, {0, 4}}};
    for (const auto gradients : footprints)
    {
        for (int b = 1; b < 10; ++b)
        {
            for (int c = 1; c < 11 - b; ++c)
            {
                const Barycentrics weights{(11 - b - c) / 11.0, b / 11.0, c / 11.0};
                const auto actual = scene.surface_material(face, weights, gradients);
                const auto expected = scene.surface_material(reference, weights, gradients);
                compare("base color", actual.base_color, expected.base_color);
                compare("emissive", actual.emission, expected.emission);
                compare("metallic", actual.metallic, expected.metallic);
                compare("roughness", actual.roughness, expected.roughness);
                compare("occlusion", actual.occlusion, expected.occlusion);

                const auto n = scene.shading_normal(transformed, face, weights, gradients, false);
                const auto ref = scene.shading_normal(baked, reference, weights, gradients, false);
                compare("world normal X", n.x, ref.x);
                compare("world normal Y", n.y, ref.y);
                compare("world normal Z", n.z, ref.z);
                const bool visible = covered({{transformed.id, face.id}, weights, gradients});
                if (visible != covered({{baked.id, reference.id}, weights, gradients}))
                {
                    throw std::runtime_error("Alpha-mask coverage differs from the baked UVs");
                }
                if (visible)
                {
                    ++solid;
                }
                else
                {
                    ++holes;
                }
            }
        }
    }
    if (solid == 0 || holes == 0)
    {
        throw std::runtime_error("Expected both covered and transparent mask samples");
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
    check_bindings(*scene);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
