import std;
import formlight.gltf;
import formlight.preview;
import formlight.raster;
import formlight.ray_query;

namespace
{
bool check_lit_box(const formlight::GltfScene &scene)
{
    using namespace formlight;
    const auto view = frame_scene(scene, {64, 48});
    const auto base = render_preview(scene, view);
    const DirectionalLighting lighting{DirectionalLight{{-2, 2, 1}}};
    const auto lit = render_preview(scene, view, lighting);
    const auto check_face = [&](Vec3 normal)
    {
        const auto point = view.camera.project(normal * 0.5).value().image;
        const int x = static_cast<int>(point.x), y = static_cast<int>(point.y);
        const auto color = base[y, x], shaded = lit[y, x];
        const auto view_direction = view.camera.ray_for_sample(pixel_center(x, y)).direction * -1;
        // BoxTextured specifies metallic zero and relies on the glTF default roughness of one.
        const MetallicRoughnessBrdf brdf{{color, {}, 0, 1}, normal, view_direction};
        const Vec3 expected = brdf.evaluate_times_cosine(lighting.light.toward_light()) * std::numbers::pi;
        const auto error = abs_components(expected - Vec3{shaded.r, shaded.g, shaded.b});
        return std::max({color.r, color.g, color.b}) > 0.01f && std::max({error.x, error.y, error.z}) < 1e-6;
    };
    // The three visible face normals in world space are +X, +Y and +Z.
    return check_face({1, 0, 0}) && check_face({0, 1, 0}) && check_face({0, 0, 1});
}
bool check_area_light()
{
    using namespace formlight;
    // Power = 4 pi^2 gives incident weight pi two units below the light center.
    const RectangleLight light{{0, 2, 0}, {2, 0, 0}, {0, 0, 2}, 4 * std::numbers::pi * std::numbers::pi};
    const Vec3 up{0, 1, 0};
    const Vec3 center = light.sample(0.5, 0.5);
    const auto white_weight = [](IncidentLight sample, double expected)
    {
        return sample.weight.x == sample.weight.y && sample.weight.y == sample.weight.z &&
               std::abs(sample.weight.x - expected) <= 1e-12;
    };
    if (!white_weight(light.incident({}, center), std::numbers::pi) ||
        !white_weight(light.incident({}, light.sample(1, 0.5)), 8 * std::numbers::pi / (5 * std::sqrt(5.0))) ||
        light.incident({0, 4, 0}, center).weight != Vec3{})
    {
        std::cerr << "Rectangle emission or diffuse geometry term disagrees with the analytic weight\n";
        return false;
    }

    const std::array mesh{
        Triangle{.vertices = {TriangleVertex{{-8, 0, -8}}, TriangleVertex{{0, 0, 8}}, TriangleVertex{{8, 0, -8}}}}};
    const MeshInstance floor{InstanceId{0}, mesh};
    const SurfaceRayOrigin receiver{floor, mesh.front(), {0.25, 0.5, 0.25}};
    const MetallicRoughnessBrdf metal{{{0.75f, 0.375f, 0.125f}, {}, 1, 0.5}, up, up};
    // Adjacent pixels on this flat receiver should both see the unit-weight light center during navigation.
    for (const auto pixel : {40u, 41u})
    {
        std::vector<LightSample> samples;
        const auto color =
            sample_area_light(light, receiver, metal, {16, 7}, pixel, {}, AreaLightPattern::Center, &samples);
        // At normal incidence the GGX lobe returns four times the metal's base RGB under this light.
        const auto error = abs_components(color - Vec3{3, 1.5, 0.5});
        if (samples.size() != 1 || samples.front().position != center || std::max({error.x, error.y, error.z}) > 1e-12)
        {
            std::cerr << "Navigation samples must hit the same rectangle center at every pixel\n";
            return false;
        }
    }
    const MetallicRoughnessBrdf white_metal{{{1, 1, 1}, {}, 1, 1}, up, up};
    std::vector<LightSample> samples;
    const auto unobstructed =
        sample_area_light(light, receiver, white_metal, {16, 7}, 0, {}, AreaLightPattern::ShiftedHalton, &samples);
    for (const auto &sample : samples)
    {
        // For F0 = 1, roughness = 1 and V = N, the BRDF times N.L reduces to c / (2 pi (1 + c)).
        const auto incident = light.incident(receiver.position(), sample.position.value());
        const double cosine = dot(up, incident.direction);
        const double expected = incident.weight.x * cosine / (2 * std::numbers::pi * (1 + cosine));
        if (std::abs(sample.direct.x - expected) > 1e-12)
        {
            std::cerr << "Rectangle BRDF must use each sampled direction\n";
            return false;
        }
    }
    const auto with_panel = [&](double height)
    {
        const std::array panels{MeshInstance{InstanceId{1}, mesh, {Basis3{}, {0, height, 0}}}};
        const TriangleBvh blockers{panels};
        return sample_area_light(light, receiver, white_metal, {16, 7}, 0, &blockers);
    };
    if (!(unobstructed.x > 0 && with_panel(1) == Vec3{} && with_panel(3) == unobstructed))
    {
        std::cerr << "Only the panel between the receiver and rectangle should cast a shadow\n";
        return false;
    }
    return true;
}

bool check_ground_shadow(formlight::GltfScene &scene)
{
    using namespace formlight;
    add_preview_ground(scene, world_bounds(scene.instances()));
    // These rays look up through the floor into the footprint of the box.
    const PinholeCamera below{{{0.85, -1.8, 1.35}, {0, -0.5, 0}, {0, 1, 0}, 0.15}, {4, 4}};
    const auto underside = clip_to_bounds(below, world_bounds(scene.instances()));
    const PreviewRenderer renderer{scene};
    for (int y = 0; y < below.extent().height; ++y)
    {
        for (int x = 0; x < below.extent().width; ++x)
        {
            const auto hit = renderer.diagnose(underside, {x, y}).surface;
            if (!hit || hit->hit.surface.id.instance != scene.instances().back().id)
            {
                std::cerr << "Box bottom shows through the opaque preview ground\n";
                return false;
            }
        }
    }
    // Look down from the shadow side so the box top does not hide its contact edge.
    const PinholeCamera overhead{{.position = {0, 4, -0.75},
                                  .target = {0, 0, -0.75},
                                  .up = {0, 0, -1},
                                  .vertical_fov_radians = std::numbers::pi / 3},
                                 {128, 128}};
    const PreviewView view{overhead, {1, 6}};
    const auto lit = render_preview(scene, view, DirectionalLighting{});
    const auto check = [&](const ColorImage &shadowed, Vec3 world, bool blocked)
    {
        const auto p = overhead.project(world).value().image;
        const int x = static_cast<int>(p.x), y = static_cast<int>(p.y);
        const auto plain = lit[y, x], actual = shadowed[y, x];
        const LinearColor expected = blocked ? LinearColor{0, 0, 0, 1} : plain;
        return plain.r > 0 && actual.r == expected.r && actual.g == expected.g && actual.b == expected.b;
    };
    // At floor height the top of the unit box projects by (-1/4, 0, -3/4).
    for (const auto method : {ShadowMethod::Map, ShadowMethod::Ray})
    {
        const auto shadowed = render_preview(scene, view, DirectionalLighting{.shadows = method});
        if (!check(shadowed, {0, -0.5, -0.55}, true) || !check(shadowed, {0, -0.5, -0.8}, true) ||
            !check(shadowed, {0, -0.5, -1.45}, false) || !check(shadowed, {0, 0.5, 0}, false))
        {
            std::cerr << (method == ShadowMethod::Map ? "Shadow map" : "Shadow ray")
                      << " disagrees with the box footprint or shadows a lit surface\n";
            return false;
        }
    }
    // From this floor pixel the first lamp is in front of the box, the second is beyond it.
    const PixelCoord floor_pixel{64, 45};
    for (const auto &[position, blocked] : {std::pair{Vec3{0, -0.3, -1}, false}, std::pair{Vec3{0, 0.5, 1}, true}})
    {
        const PreviewRenderer point_renderer{scene, PointLighting{PointLight{position, {1, 1, 1}, 4}}};
        const auto report = point_renderer.diagnose(view, floor_pixel);
        const auto pixel = point_renderer.render(view)[floor_pixel.y, floor_pixel.x];
        if (report.lights.at(0).samples.size() != 1 || report.lights.at(0).samples[0].position != position ||
            pixel.r != report.color.r || (pixel.r > 0) == blocked ||
            report.lights.at(0).samples[0].shadow.state != (blocked ? ShadowState::Blocked : ShadowState::Visible))
        {
            std::cerr << "Point shadow must end at the lamp and agree with pixel inspection\n";
            return false;
        }
        if (blocked && report.lights.at(0).samples[0].shadow.blocker.value().instance != scene.instances().front().id)
        {
            std::cerr << "Point light diagnostic identifies the wrong blocker\n";
            return false;
        }
    }
    const AreaLighting area{RectangleLight{{0, 2.5, 1.5}, {1, 0, 0}, {0, 0, 1}, 240}, {64, 7}};
    const auto frame = render_preview(scene, view, area);
    auto unshadowed = area;
    unshadowed.shadows = false;
    struct Probe
    {
        double z;
        int min_blocked, max_blocked;
    };
    // At z = -1.5 the top edge of the box cuts through the center of the rectangle.
    for (const auto [z, min_blocked, max_blocked] : {Probe{-0.6, 64, 64}, Probe{-1.5, 20, 44}, Probe{-2.4, 0, 0}})
    {
        const auto p = overhead.project({0, -0.5, z}).value().image;
        const PixelCoord pixel{static_cast<int>(p.x), static_cast<int>(p.y)};
        const auto report = diagnose_pixel(scene, view, pixel, area);
        const auto &floor = scene.instances().back();
        if (!report.surface || report.surface->hit.surface.id.instance != floor.id ||
            report.surface->material_index != floor.triangles.front().material_index ||
            report.lights.at(0).samples.size() != 64)
        {
            std::cerr << "Expected 64 rectangle samples on the ground material\n";
            return false;
        }
        int blocked = 0;
        Vec3 sum{};
        for (std::size_t i = 0; i < report.lights.at(0).samples.size(); ++i)
        {
            const auto &shadow = report.lights.at(0).samples[i].shadow;
            if (shadow.state == ShadowState::Blocked)
            {
                if (!shadow.blocker || shadow.blocker->instance != scene.instances().front().id)
                {
                    std::cerr << "Expected the box to block the ground sample\n";
                    return false;
                }
                ++blocked;
            }
            sum = sum + report.sample_contribution(0, i);
        }
        const auto color = frame[pixel.y, pixel.x];
        if (min_blocked != max_blocked)
        {
            const auto full = diagnose_pixel(scene, view, pixel, unshadowed).color.r;
            if (!(color.r > 0.3f * full && color.r < 0.7f * full))
            {
                std::cerr << "Expected partial light at the edge of the box shadow\n";
                return false;
            }
        }
        if (std::tie(color.r, color.g, color.b) != std::tie(report.color.r, report.color.g, report.color.b) ||
            std::max({std::abs(sum.x - color.r), std::abs(sum.y - color.g), std::abs(sum.z - color.b)}) > 1e-6 ||
            blocked < min_blocked || blocked > max_blocked)
        {
            std::cerr << std::format("Pixel replay at z={} disagrees with the shadow footprint or linear RGB\n", z);
            return false;
        }
    }
    return true;
}
} // namespace

auto main(int argc, char **argv) -> int
{
    using namespace formlight;
    if (argc != 2)
    {
        std::cerr << "Expected the BoxTextured.glb fixture path\n";
        return 1;
    }
    auto scene = GltfScene::load(argv[1], GltfContent::Materials);
    if (!scene)
    {
        std::cerr << scene.error() << '\n';
        return 1;
    }
    const auto instances = scene->instances();
    if (scene->mesh_count() != 1 || instances.size() != 1 || instances.front().triangles.size() != 12)
    {
        std::cerr << "Expected one box mesh, one instance and twelve triangles\n";
        return 1;
    }
    if (instances.front().triangles[6].vertices.front().normal.y != -1)
    {
        std::cerr << "Expected the authored local -Y normal on the box front face\n";
        return 1;
    }
    constexpr ImageExtent extent{64, 48};
    constexpr double camera_x = 1.0 / 48;
    const PinholeCamera camera{{.position = {camera_x, 0, 2.5},
                                .target = {camera_x, 0, 0},
                                .up = {0, 1, 0},
                                .vertical_fov_radians = std::numbers::pi / 2},
                               extent};
    std::vector<std::optional<RasterHit>> pixels(extent.width * extent.height);
    const VisibilityBuffer buffer{pixels.data(), extent.height, extent.width};
    const auto image = render_preview(*scene, {camera, {1, 4}});
    for (const auto &triangle : RasterTriangle::prepare(camera, instances, {1, 4}))
    {
        triangle.rasterize(buffer);
    }
    for (int y = 0; y < extent.height; ++y)
    {
        for (int x = 0; x < extent.width; ++x)
        {
            const auto point = pixel_center(x, y);
            const auto ray = camera.ray_for_sample(point);
            const auto hit = intersect(ray, instances, {0, std::numeric_limits<double>::infinity()});
            const auto &pixel = buffer[y, x];
            const bool visible = x >= 26 && x < 38 && y >= 18 && y < 30;
            if (pixel.has_value() != visible || hit.has_value() != visible)
            {
                std::cerr << std::format("Box silhouette differs at ({}, {})\n", x, y);
                return 1;
            }
            if (visible)
            {
                // The parent's rotation puts local -Y at world +Z. The face uses U in [3,4].
                const auto id = std::to_underlying(pixel->surface.id.primitive);
                if (pixel->surface.id != hit->surface.id || pixel->surface.id.instance != InstanceId{1} ||
                    (id != 6 && id != 7))
                {
                    std::cerr << std::format("Wrong front face at ({}, {})\n", x, y);
                    return 1;
                }
                const auto &face = instances.front().triangles[id];
                const auto raster_uv = face.uv_at(pixel->surface.weights);
                const auto ray_uv = face.uv_at(hit->surface.weights);
                const double u = 3.5 - (camera_x + (point.x - 32) / 12);
                const double v = 0.5 - (24 - point.y) / 12;
                constexpr double tolerance = 1e-12;
                if (!(std::abs(pixel->view_depth - 2) <= tolerance &&
                      std::abs(camera.to_view(ray.at(hit->t)).depth - 2) <= tolerance &&
                      std::abs(raster_uv.u - u) <= tolerance && std::abs(raster_uv.v - v) <= tolerance &&
                      std::abs(ray_uv.u - u) <= tolerance && std::abs(ray_uv.v - v) <= tolerance))
                {
                    std::cerr << std::format("Box depth or UV differs at ({}, {})\n", x, y);
                    return 1;
                }
            }
        }
    }
    // The 256px PNG spans 12 screen pixels. Its sampler blends nearest samples of levels 4 and 5.
    // These linear references use 16x16 and 32x32 box averages of the original sRGB texels.
    const auto matches_rgb = [&](int x, int y, Vec3 expected)
    {
        const auto color = image[y, x];
        const auto delta = abs_components(Vec3{color.r, color.g, color.b} - expected);
        return std::max({delta.x, delta.y, delta.z}) < 1e-6;
    };
    if (!matches_rgb(32, 19, {0.14995979, 0.41788507, 0.73791041}) ||
        !matches_rgb(32, 27, {0.10702310, 0.24228112, 0.02028856}))
    {
        std::cerr << "Repeated base-color texture has the wrong colors or orientation\n";
        return 1;
    }
    if (!check_lit_box(*scene))
    {
        std::cerr << "Directional light disagrees with the world-space box face normals\n";
        return 1;
    }
    // On a repeat boundary, linear filtering blends the last and first texel.
    ColorImage seam{{2, 1}};
    seam[0, 1] = {1, 1, 1, 1};
    if (TextureImage{std::move(seam)}.sample({-1, 0.5}, {}).r != 0.5f)
    {
        std::cerr << "Bilinear repeat failed across the negative U boundary\n";
        return 1;
    }
    if (!check_ground_shadow(*scene) || !check_area_light())
    {
        return 1;
    }
    return 0;
}
