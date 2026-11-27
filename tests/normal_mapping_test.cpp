import std;
import formlight.preview;
import formlight.scene_toml;

namespace
{
using namespace formlight;

void near(Vec3 actual, Vec3 expected, std::string_view label)
{
    const auto d = abs_components(actual - expected);
    if (!(std::max({d.x, d.y, d.z}) < 1e-6))
    {
        throw std::runtime_error(std::format("{}: got ({}, {}, {}), expected ({}, {}, {})", label, actual.x, actual.y,
                                             actual.z, expected.x, expected.y, expected.z));
    }
}

Vec3 unit(Vec3 v)
{
    return v / std::hypot(v.x, v.y, v.z);
}

void check_panels(const SceneConfig &config)
{
    const auto request = prepare_render(config);
    const auto preview = load_preview(request);
    const PreviewRenderer renderer{preview.scene, request.lighting};
    const auto frame = renderer.render(preview.view);
    if (preview.scene.instances().size() != 5)
    {
        throw std::runtime_error("Expected five tangent-frame probes");
    }
    for (const auto &instance : preview.scene.instances())
    {
        const auto id = std::to_underlying(instance.id);
        const bool mirrored = id == 2 || id == 3;
        const Vec3 scale = mirrored ? Vec3{-1.5, 0.65, 1.2} : Vec3{1, 1, 1};
        // Local N=(1,-1,1), T=(1,1,0), and UV1 reverses B. A reflection also reverses handedness.
        const Vec3 n = unit({1 / scale.x, -1 / scale.y, 1 / scale.z});
        const Vec3 t = unit({scale.x, scale.y, 0});
        const Vec3 b = cross(n, t) * (mirrored ? 1 : -1);
        const auto &face = instance.triangles.front();
        for (const double weight_b : {0.2, 0.7})
        {
            const Barycentrics weights{0.8 - weight_b, weight_b, 0.2};
            const double green = weight_b < 0.5 ? 191 : 64;
            const Vec3 expected =
                unit(t * (0.5 * (2 * 191.0 / 255 - 1)) + b * (0.5 * (2 * green / 255 - 1)) + n * (2 * 218.0 / 255 - 1));
            near(preview.scene.shading_normal(instance, face, weights, {}, true), expected * -1, "Back face");
            const auto p =
                preview.view.camera.project(instance.transform.to_world_point(face.position_at(weights))).value().image;
            const PixelCoord pixel{static_cast<int>(p.x), static_cast<int>(p.y)};
            const auto report = renderer.diagnose(preview.view, pixel);
            const auto &sample = report.samples.at(0);
            if (!sample.surface || sample.surface->hit.surface.id.instance != instance.id)
            {
                throw std::runtime_error("Normal map probe missed its triangle");
            }
            near(sample.surface->shading_normal.value(), expected, "World shading normal");
            const auto color = frame[pixel.y, pixel.x];
            const Vec3 eye = preview.view.camera.ray_for_sample(pixel_center(pixel.x, pixel.y)).direction * -1;
            const MetallicRoughnessBrdf brdf{sample.surface->material, expected, eye};
            near({color.r, color.g, color.b}, brdf.evaluate_times_cosine(unit({0.5, -0.3, 1})) * std::numbers::pi,
                 "Normal-mapped direct lighting");
        }
        // A large UV footprint selects the final mip. AO must retain its scalar red channel.
        const Barycentrics center{0.4, 0.4, 0.2};
        const WeightGradients wide{{10, 0}, {0, 10}};
        const auto material = preview.scene.surface_material(face, center, wide);
        if (std::abs(material.occlusion - 191.0 / 255) > 1e-6)
        {
            throw std::runtime_error("AO sampled a normal mip");
        }
        const auto sampled = preview.scene.shading_normal(instance, face, center, wide, false);
        near(sampled, unit(t * (0.5 * (2 * 191.0 / 255 - 1)) + n * (2 * 218.0 / 255 - 1)), "Normal mip");
    }
}

void check_mip_vectors()
{
    ColorImage pixels{{4, 1}};
    // The first pair averages to (+X +Y)/2. The second pair cancels to a zero vector.
    pixels[0, 0] = {1, 0.5f, 0.5f};
    pixels[0, 1] = {0.5f, 1, 0.5f};
    pixels[0, 2] = {1, 0.5f, 0.5f};
    pixels[0, 3] = {0, 0.5f, 0.5f};
    TextureImage normals{std::move(pixels), TextureUsage::Normal};
    normals.generate_mipmaps();
    const auto decode = [&](double u, double footprint)
    {
        const auto sample =
            normals.sample({u, 0.5}, {.min = TexelFilter::Nearest, .mip = MipFilter::Nearest}, {{footprint, 0}, {}});
        return Vec3{2 * sample.r - 1, 2 * sample.g - 1, 2 * sample.b - 1};
    };
    const Vec3 diagonal = unit({1, 1, 0});
    near(decode(0.25, 0.5), diagonal, "Normalized first mip");
    near(decode(0.75, 0.5), {0, 0, 1}, "Cancelling normals");
    near(decode(0.5, 1), unit(diagonal + Vec3{0, 0, 1}), "Normalized second mip");
}
} // namespace

int main(int argc, char **argv)
try
{
    if (argc != 2)
    {
        return 1;
    }
    auto config = formlight::load_scene_config(argv[1]);
    check_mip_vectors();
    for (auto mode : {formlight::RenderMode::Raster, formlight::RenderMode::Hybrid})
    {
        config.render.mode = mode;
        check_panels(config);
    }
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
