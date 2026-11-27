import std;
import formlight.preview;
import formlight.ray_query;

using namespace formlight;

int main(int argc, char **argv)
try
{
    if (argc != 2)
    {
        return 1;
    }
    const auto scene = GltfScene::load(argv[1], GltfContent::Materials);
    if (!scene)
    {
        throw std::runtime_error(scene.error());
    }
    const PinholeCamera camera{{{0, 1.3, 4}, {0, 1.3, 0}, {0, 1, 0}, std::numbers::pi / 3}, {64, 64}};
    const auto view = clip_to_bounds(camera, world_bounds(scene->instances()));
    const Vec3 coral_normal = unit_vector({0.4, 0, 1}, "Invalid coral normal");
    const Vec3 blue_normal = unit_vector({-0.4, 0, 1}, "Invalid blue normal");
    for (const auto shadows : {ShadowMethod::Map, ShadowMethod::Ray})
    {
        const PreviewRenderer renderer{*scene, DirectionalLighting{DirectionalLight{{0, 0, 1}}, shadows}};
        const auto frame = renderer.render(view);
        const auto clear = frame[0, 0];
        const Vec3 background{clear.r, clear.g, clear.b};
        const auto check = [&](Vec3 world, InstanceId front, double coral_share, double blue_share, double stone_share)
        {
            const auto p = camera.project(world).value().image;
            const PixelCoord pixel{static_cast<int>(p.x), static_cast<int>(p.y)};
            const auto ray = camera.ray_for_sample(pixel_center(pixel.x, pixel.y));
            const Vec3 to_eye = ray.direction * -1;
            const auto lit = [&](LinearColor base, Vec3 normal)
            {
                const MetallicRoughnessBrdf brdf{{base, {}, 0, 1}, normal, to_eye};
                return brdf.evaluate_times_cosine({0, 0, 1}) * std::numbers::pi;
            };
            Vec3 stone_color{};
            if (stone_share > 0)
            {
                // Sample the opaque block alone, including its interpolated shading normal.
                const auto &stone = scene->instance(InstanceId{2});
                const auto hit = intersect(ray, std::span{&stone, 1}, {0, std::numeric_limits<double>::infinity()});
                if (!hit)
                {
                    throw std::runtime_error("Expected the probe ray to hit the opaque block");
                }
                const auto &face = stone.triangles[std::to_underlying(hit->surface.id.primitive)];
                stone_color =
                    lit({0.76f, 0.70f, 0.54f}, scene->shading_normal(stone, face, hit->surface.weights, {}, false));
            }
            const Vec3 expected = lit({1, 0.12f, 0.05f}, coral_normal) * coral_share +
                                  lit({0.04f, 0.34f, 1}, blue_normal) * blue_share + stone_color * stone_share +
                                  background * (1 - coral_share - blue_share - stone_share);
            const auto color = frame[pixel.y, pixel.x];
            const auto report = renderer.diagnose(view, pixel);
            const auto delta = abs_components(Vec3{color.r, color.g, color.b} - expected);
            if (std::max({delta.x, delta.y, delta.z}) > 1e-6 || color.a != 1 || !report.surface ||
                report.surface->hit.surface.id.instance != front || report.lights.at(0).samples.size() != 1 ||
                std::tie(color.r, color.g, color.b) != std::tie(report.color.r, report.color.g, report.color.b))
            {
                throw std::runtime_error(std::format("Incorrect blend or pick at pixel ({}, {})", pixel.x, pixel.y));
            }
        };
        check({-0.7, 1.6, 0}, InstanceId{0}, 0.5, 0.25, 0);
        check({0.7, 1.6, 0}, InstanceId{1}, 0.25, 0.5, 0);
        // The block occludes one or both sheets. Transparent sheets cast no shadow on it.
        check({0.7, 0.72, 0.15}, InstanceId{1}, 0, 0.5, 0.5);
        check({0, 0.72, 0.15}, InstanceId{2}, 0, 0, 1);
    }
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
