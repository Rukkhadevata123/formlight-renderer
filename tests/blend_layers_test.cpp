import std;
import formlight.preview;
import formlight.ray_query;

using namespace formlight;

namespace
{
void check_thin_glass(const std::filesystem::path &asset)
{
    const auto scene = GltfScene::load(asset, GltfContent::Materials);
    if (!scene)
    {
        throw std::runtime_error(scene.error());
    }
    const PinholeCamera camera{{{0, 0, 3}, {}, {0, 1, 0}, std::numbers::pi / 4}, {33, 33}};
    const auto view = clip_to_bounds(camera, scene->bounds());
    const Vec3 toward_light{0.28, 0, 0.96};
    const DirectionalLighting light{DirectionalLight{toward_light}, ShadowMethod::None};
    // A smooth dielectric reflects 4% at normal incidence. Its direct highlight survives full transmission.
    const MetallicRoughnessBrdf black_dielectric{{{0, 0, 0}, {}, 0, 0}, {0, 0, 1}, {0, 0, 1}};
    const auto highlight = black_dielectric.evaluate_times_cosine(toward_light) * std::numbers::pi;
    const auto expected = Vec3{0.5 * 0.2, 0.4, 0.25 * 0.8} * 0.96 + highlight;
    for (const auto mode : {RenderMode::Raster, RenderMode::Hybrid})
    {
        const PreviewRenderer renderer{*scene, light, mode};
        const auto color = renderer.render(view)[16, 16];
        const auto report = renderer.diagnose(view, {16, 16});
        const auto &surface = report.samples.at(0).surface;
        const auto delta = abs_components(Vec3{color.r, color.g, color.b} - expected);
        if (std::max({delta.x, delta.y, delta.z}) > 1e-6 || !surface ||
            surface->hit.surface.id.instance != InstanceId{0} ||
            std::tie(color.r, color.g, color.b) != std::tie(report.color.r, report.color.g, report.color.b))
        {
            throw std::runtime_error("Thin glass lost the tinted background, highlight or front surface pick");
        }
    }
}
void check_frosted_glass(const std::filesystem::path &asset)
{
    const auto scene = GltfScene::load(asset, GltfContent::Materials);
    if (!scene)
    {
        throw std::runtime_error(scene.error());
    }
    const PinholeCamera camera{{{0, 0, 3}, {}, {0, 1, 0}, std::numbers::pi / 4}, {257, 193}};
    const auto view = clip_to_bounds(camera, scene->bounds());
    const PreviewRenderer frosted{*scene, PreviewLighting{}};
    const auto frame = frosted.render(view);
    for (const int x : {127, 128})
    {
        const auto replay = frosted.diagnose(view, {x, 96});
        const auto g = frame[96, x].g; // Adjacent dark and bright stripes would give 0 and 0.96 through smooth glass.
        if (!(0.25 < g && g < 0.75) || replay.color.g != g)
        {
            throw std::runtime_error("Frosted stripes did not mix neighboring radiance consistently");
        }
    }

    // Both coplanar pane instances cover this receiver; the smaller pane's diagonal must count only once.
    const PinholeCamera inside{{{0.75, 0, -0.2}, {0.75, 0, -0.5}, {0, 1, 0}, std::numbers::pi / 4}, {9, 9}};
    for (const auto method : {ShadowMethod::Map, ShadowMethod::Ray})
    {
        const PreviewRenderer renderer{*scene, DirectionalLighting{DirectionalLight{{0, 0, 1}}, method, true}};
        const auto report = renderer.diagnose(clip_to_bounds(inside, scene->bounds()), {4, 4});
        const auto &sample = report.samples.at(0).lights.at(0).samples.at(0);
        const Vec3 pane{0.48, 0.96, 0.24};
        // At normal incidence the white, rough receiver's direct response is 0.96 diffuse + 0.01 specular.
        const auto delta = abs_components(sample.direct - component_product(pane, pane) * 0.97);
        if (sample.shadow.state != ShadowState::PartiallyVisible || std::max({delta.x, delta.y, delta.z}) > 1e-6)
        {
            throw std::runtime_error("The receiver did not receive the expected tinted directional light");
        }
    }
}
void check_volume(const std::filesystem::path &asset)
{
    const auto scene = GltfScene::load(asset, GltfContent::Materials);
    if (!scene)
    {
        throw std::runtime_error(scene.error());
    }
    const PinholeCamera camera{{{0, 0, 3}, {}, {0, 1, 0}, std::numbers::pi / 4}, {257, 193}};
    const auto view = clip_to_bounds(camera, scene->bounds());
    const PreviewRenderer renderer{*scene, PreviewLighting{}};
    const auto frame = renderer.render(view);
    const auto center = frame[96, 128];
    // Mesh thickness 0.5, texture G = 128/255, Z scale 2, attenuation distance 0.5 and IOR 2.
    constexpr double exponent = 2 * 128.0 / 255;
    const Vec3 expected = Vec3{std::pow(0.25, exponent), std::pow(0.5, exponent), 1} * (8.0 / 9);
    const auto error = abs_components(Vec3{center.r, center.g, center.b} - expected);
    if (std::max({error.x, error.y, error.z}) > 1e-6)
    {
        throw std::runtime_error("Volume slab center disagrees with analytic Fresnel and Beer attenuation");
    }
    // The straight ray misses the white panel. Snell refraction moves the lookup 2.65 px left, onto white.
    const auto edge = renderer.diagnose(view, {165, 96});
    const auto &surface = edge.samples.at(0).surface;
    if (!surface || !surface->refraction || std::abs(surface->refraction->offset.x + 2.65) > 0.01 ||
        std::abs(edge.color.b - 8.0 / 9) > 1e-6 || edge.color.b != frame[96, 165].b)
    {
        throw std::runtime_error("Volume slab did not refract the background edge identically in the frame and pick");
    }
}
} // namespace

int main(int argc, char **argv)
try
{
    if (argc != 5)
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
            const auto &sample = report.samples.at(0);
            const auto delta = abs_components(Vec3{color.r, color.g, color.b} - expected);
            if (std::max({delta.x, delta.y, delta.z}) > 1e-6 || color.a != 1 || !sample.surface ||
                sample.surface->hit.surface.id.instance != front || sample.lights.at(0).samples.size() != 1 ||
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
    check_thin_glass(argv[2]);
    check_frosted_glass(argv[3]);
    check_volume(argv[4]);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
