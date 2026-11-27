import std;
import formlight.preview;
import formlight.ray_query;
import formlight.scene_toml;

using namespace formlight;

int main(int argc, char **argv)
try
{
    if (argc != 2)
    {
        return 1;
    }
    auto config = load_scene_config(argv[1]);
    config.render.extent = {320, 240};
    const auto preview = load_preview(prepare_render(config));
    const auto &scene = preview.scene;
    const auto accept = scene.solid_filter();
    const TriangleBvh bvh{scene.instances(), accept};

    // Cross the panel at t=1 through a hole. The front face of the block lies at t=1.45.
    const Ray ray{{-0.4, 1.2, 1}, {0, 0, -1}};
    const auto nearest = bvh.intersect(ray, {0, 2});
    const auto reference = intersect(ray, scene.instances(), {0, 2}, accept);
    if (!nearest || !reference || nearest->surface.id != reference->surface.id ||
        nearest->surface.id.instance != InstanceId{1} || bvh.occluded(ray, {0, 1.25}) ||
        occluded(ray, scene.instances(), {0, 1.25}, accept) || !bvh.occluded(ray, {0, 2}))
    {
        throw std::runtime_error("The panel hole hid the block or occluded a ray ending before it");
    }

    for (const auto mode : {RenderMode::Raster, RenderMode::Hybrid})
    {
        config.render.mode = mode;
        const PreviewRenderer renderer{scene, prepare_render(config).lighting};
        const auto frame = renderer.render(preview.view);
        const auto probe = [&](Vec3 point, InstanceId instance, ShadowState shadow)
        {
            const auto p = preview.view.camera.project(point).value().image;
            const PixelCoord pixel{static_cast<int>(p.x), static_cast<int>(p.y)};
            const auto report = renderer.diagnose(preview.view, pixel);
            const auto color = frame[pixel.y, pixel.x];
            if (!report.surface || report.surface->hit.surface.id.instance != instance ||
                report.lights.at(0).samples.size() != 1 || report.lights.at(0).samples.front().shadow.state != shadow ||
                (color.r > 0) != (shadow == ShadowState::Visible) ||
                std::tie(color.r, color.g, color.b) != std::tie(report.color.r, report.color.g, report.color.b))
            {
                throw std::runtime_error(
                    std::format("Wrong mask coverage or shadow at pixel ({}, {})", pixel.x, pixel.y));
            }
        };
        probe({0, 1.2, 0}, InstanceId{0}, ShadowState::Visible);
        // The camera sees the lit block through one of the holes.
        probe({-0.4, 1.07, 0}, InstanceId{1}, ShadowState::Visible);
        // Project a hole and a solid strip onto the floor along the directional light.
        probe({1.9, 0, -1.2}, InstanceId{2}, ShadowState::Visible);
        probe({2.3, 0, -1.2}, InstanceId{2}, ShadowState::Blocked);
    }
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
