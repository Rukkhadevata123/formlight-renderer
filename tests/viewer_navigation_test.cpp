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

const ColorImage &await_frame(ViewerSession &session, ViewerSession::Clock::time_point now)
{
    while (true)
    {
        if (const auto *image = session.update({}, now))
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
    config.render.extent = {81, 61};
    config.render.mode = RenderMode::Hybrid;
    config.render.sampling = {16, 7};
    config.light.type = LightKind::Rectangle;
    config.camera.auto_frame = false;
    config.camera.position = {0.25, 0, 4};
    config.camera.target = {0.25, 0, 0};
    const auto request = prepare_render(config);
    ViewerSession session{request};
    auto now = ViewerSession::Clock::now();
    if (await_frame(session, now).extent().width != 81 || session.updating())
    {
        throw std::runtime_error("Expected an initial full-quality frame");
    }
    (void)session.update({.orbit_pixels = {40, 0}}, now);
    // Submit a new pose on every poll. Navigation must keep publishing, even at full quality.
    double dx = 0.002;
    for (int frames = 0; frames < 2;)
    {
        if (const auto *image = session.update({.pan_heights = {dx, 0}}, now))
        {
            const auto [w, h] = image->extent();
            if (!(w == 81 && h == 61) && !(session.showing_preview() && w == 41 && h == 31))
            {
                throw std::runtime_error("Unexpected navigation frame dimensions");
            }
            ++frames;
        }
        dx = -dx;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    const auto &navigation = await_frame(session, now);
    const PixelCoord center{navigation.extent().width / 2, navigation.extent().height / 2};
    const auto preview_color = navigation[center.y, center.x];
    const auto expected_samples = session.showing_preview() ? 1u : config.render.sampling.samples;
    (void)session.update({.picked = center}, now);
    while (!session.report())
    {
        (void)session.update({}, now);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    const auto &pick = *session.report();
    if (!pick.surface || pick.surface->hit.surface.id.instance != InstanceId{1} ||
        pick.samples.size() != expected_samples || pick.sampling_seed != 7 ||
        std::tie(preview_color.r, preview_color.g, preview_color.b) !=
            std::tie(pick.color.r, pick.color.g, pick.color.b))
    {
        throw std::runtime_error("Pick did not reproduce the displayed preview sample");
    }
    (void)session.update({.focus = true}, now);
    (void)await_frame(session, now);
    now += std::chrono::seconds{1};
    (void)session.update({}, now); // Allow any refinement of the focused pose, then interrupt with another gesture.
    (void)session.update({.wheel = 1}, now);
    const auto &zoomed = await_frame(session, now);
    if (session.selection() || session.report())
    {
        throw std::runtime_error("Old refinement or selection survived a new gesture");
    }
    const auto &latest = session.showing_preview() ? await_frame(session, now + std::chrono::seconds{1}) : zoomed;
    if (session.updating() || latest.extent().width != 81 || latest.extent().height != 61)
    {
        throw std::runtime_error("The settled camera did not return to full resolution");
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
