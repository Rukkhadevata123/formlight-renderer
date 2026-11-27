import std;
import formlight.preview;
import formlight.ray_query;

namespace
{
using namespace formlight;

void check_samples(const SampleDiagnostic &sample, ShadowMethod method, bool blocked, InstanceId panel_id,
                   const std::string &label)
{
    for (const auto &light : sample.lights)
    {
        if (light.samples.size() != (light.scene_index == 2 ? 4u : 1u))
        {
            throw std::runtime_error(label + ": missing shadow samples");
        }
        for (const auto &sample : light.samples)
        {
            const auto &shadow = sample.shadow;
            if (shadow.state != (blocked ? ShadowState::Blocked : ShadowState::Visible))
            {
                throw std::runtime_error(std::format("{}: wrong shadow for light {}", label, light.scene_index));
            }
            if (blocked && (method == ShadowMethod::Ray || light.scene_index != 0) &&
                shadow.blocker.value().instance != panel_id)
            {
                throw std::runtime_error(label + ": incorrect BVH blocker ID");
            }
        }
    }
}

void check_shadows(const std::filesystem::path &model, double side)
{
    auto scene = GltfScene::load(model, GltfContent::Materials);
    if (!scene)
    {
        throw std::runtime_error(scene.error());
    }
    if (scene->instances().size() != 4)
    {
        throw std::runtime_error("Expected four caster panels");
    }
    // Put the camera between the four panels and the receiver so it sees their shadows directly.
    const double z = -6 * side;
    const TriangleVertex a{{-4, -4, z}}, b{{4, -4, z}}, c{{4, 4, z}}, d{{-4, 4, z}};
    scene->add_solid_mesh({Triangle{.vertices = {a, b, c}}, Triangle{.vertices = {a, c, d}}}, {1, 1, 1});
    const PinholeCamera camera{{{0, 0, -0.5 * side}, {0, 0, z}, {0, 1, 0}, std::numbers::pi / 3}, {96, 96}};
    const auto view = clip_to_bounds(camera, world_bounds(scene->instances()));
    const auto panels = scene->instances().first(4);
    const auto coverage = scene->solid_filter();
    const auto ray_culling = scene->face_culling(CullFace::Front);
    const Vec3 toward_light{0, 0, side}, lamp{0, 0, 60 * side};
    for (const auto method : {ShadowMethod::Map, ShadowMethod::Ray})
    {
        PreviewLighting lighting{DirectionalLighting{DirectionalLight{toward_light}, method}};
        lighting.direct.push_back({1, PointLighting{PointLight{lamp, {1, 1, 1}, 100}}});
        lighting.direct.push_back({2, AreaLighting{RectangleLight{lamp, {1, 0, 0}, {0, -side, 0}, 500}, {4, 7}}});
        const PreviewRenderer renderer{*scene, lighting};
        const auto frame = renderer.render(view);
        for (const auto &panel : panels)
        {
            const auto id = std::to_underlying(panel.id);
            const bool double_sided = id == 1 || id == 3;
            const bool masked = id >= 2;
            for (const double x : {-0.5, 0.5})
            {
                const Vec3 receiver = panel.transform.to_world_point({x, 0, 0}) + Vec3{0, 0, z};
                const bool blocked = (side > 0 || double_sided) && (!masked || x > 0);
                const auto label = std::format("panel {}, local x {}, light z {}", id, x, lamp.z);
                const Ray ray{receiver, toward_light};
                const auto nearest = intersect(ray, panels, {0, 10}, coverage, ray_culling);
                if (occluded(ray, panels, {0, 10}, coverage, ray_culling) != blocked ||
                    nearest.has_value() != blocked || (nearest && nearest->surface.id.instance != panel.id))
                {
                    throw std::runtime_error(label + ": incorrect brute-force blocker");
                }
                const auto p = camera.project(receiver).value().image;
                const PixelCoord pixel{static_cast<int>(p.x), static_cast<int>(p.y)};
                const auto report = renderer.diagnose(view, pixel);
                const auto &sample = report.samples.at(0);
                if (!sample.surface || sample.surface->hit.surface.id.instance != scene->instances().back().id ||
                    sample.lights.size() != 3)
                {
                    throw std::runtime_error(label + ": expected three lights on the receiver");
                }
                check_samples(sample, method, blocked, panel.id, label);
                const auto color = frame[pixel.y, pixel.x];
                if ((color.r > 0) == blocked ||
                    std::tie(color.r, color.g, color.b) != std::tie(report.color.r, report.color.g, report.color.b))
                {
                    throw std::runtime_error(label + ": rendered shadow differs from pixel replay");
                }
            }
        }
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
    check_shadows(argv[1], 1);
    check_shadows(argv[1], -1);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
