import std;
import formlight.preview;
import formlight.viewer.orbit_camera;
import formlight.viewer.session;

namespace
{
using namespace formlight;

void check_pan_from_above()
{
    const CameraView overhead{{0, 4, 0}, {}, {0, 0, -1}, std::numbers::pi / 2};
    OrbitCamera orbit{PinholeCamera{overhead, {80, 40}}};
    orbit.pan(0.25, 0.125);
    orbit.zoom(1);
    const auto pose = orbit.view({{-1, -1, -1}, {1, 1, 1}}).camera.view();
    if (std::abs(pose.target.x + 2) > 1e-12 || std::abs(pose.target.z + 1) > 1e-12 ||
        std::abs(pose.position.y - 4 / 1.15) > 1e-12 || std::abs(pose.up.z + 1) > 1e-12)
    {
        throw std::runtime_error("Pan or zoom changed the axes of the authored top view");
    }
}

const ColorImage &await_frame(ViewerSession &session)
{
    while (true)
    {
        if (const auto *image = session.update({}))
        {
            return *image;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
}

void check_frame_replacement(const std::filesystem::path &asset)
{
    SceneConfig config;
    config.scene = {asset, true};
    config.render.extent = {80, 60};
    config.render.mode = RenderMode::Hybrid;
    config.light.type = LightKind::Directional;
    config.camera.auto_frame = false;
    config.camera.position = {0.25, 0, 4};
    config.camera.target = {0.25, 0, 0};
    const auto request = prepare_render(config);
    ViewerSession session{request};
    constexpr PixelCoord center{40, 30};
    const auto old_color = await_frame(session)[center.y, center.x];

    // Keep superseding the render while inspecting the image that is still on screen.
    (void)session.update({.orbit_pixels = {40, 0}, .picked = center});
    while (!session.report())
    {
        if (session.update({.pan_heights = {0.01, 0}}))
        {
            throw std::runtime_error("Published a superseded view");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    const auto &pick = *session.report();
    if (!pick.surface || pick.surface->hit.surface.id.instance != InstanceId{1} ||
        std::tie(old_color.r, old_color.g, old_color.b) != std::tie(pick.color.r, pick.color.g, pick.color.b))
    {
        throw std::runtime_error("Pick did not use the displayed camera");
    }
    (void)session.update({.focus = true});
    (void)session.update({.wheel = 1});
    const auto &latest = await_frame(session);
    if (session.selection() || session.report())
    {
        throw std::runtime_error("Old selection survived frame replacement");
    }

    // The box is centered at the origin with unit sides. Focus discards the preceding pan.
    const double yaw = -40 * (0.35 * std::numbers::pi / 180);
    const double distance = framing_distance(std::sqrt(0.75), request.extent, request.vertical_fov_radians) / 1.15;
    config.camera.target = {};
    config.camera.position = Vec3{std::sin(yaw), 0, std::cos(yaw)} * distance;
    const auto reference = render_scene(prepare_render(config));
    for (int y = 0; y < request.extent.height; ++y)
    {
        for (int x = 0; x < request.extent.width; ++x)
        {
            const auto a = latest[y, x], b = reference[y, x];
            if (std::max({std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b)}) > 1e-5f)
            {
                throw std::runtime_error("Latest frame differs from the focused and zoomed CLI camera");
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
    check_pan_from_above();
    check_frame_replacement(argv[1]);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
