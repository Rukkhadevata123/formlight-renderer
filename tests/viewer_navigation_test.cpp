import std;
import formlight.preview;
import formlight.viewer.document;
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

void check_image(const ColorImage &actual, const SceneConfig &config)
{
    const auto reference = render_scene(prepare_render(config));
    const auto [width, height] = reference.extent();
    if (actual.extent().width != width || actual.extent().height != height)
    {
        throw std::runtime_error("Displayed frame has the wrong resolution");
    }
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto a = actual[y, x], b = reference[y, x];
            if (std::max({std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b)}) > 1e-5f)
            {
                throw std::runtime_error(std::format("Displayed {}x{} frame differs from CLI at ({}, {}): red {} vs {}",
                                                     width, height, x, y, a.r, b.r));
            }
        }
    }
}

void check_frame_replacement(const std::filesystem::path &asset, const std::filesystem::path &external_model)
{
    SceneConfig config;
    config.scene = {asset, true};
    config.render.extent = {81, 61};
    config.render.mode = RenderMode::Hybrid;
    config.render.sampling = {16, 7};
    config.lighting.emplace().lights.push_back({.type = LightKind::Rectangle});
    config.lighting->lights.push_back({.type = LightKind::Directional});
    config.camera.auto_frame = false;
    config.camera.position = {0.25, 0, 4};
    config.camera.target = {0.25, 0, 0};
    const auto request = prepare_render(config);
    const auto source = asset.parent_path() / "scene.toml";
    ViewerDocument document{source, config};
    auto &session = document.session();
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
        pick.lights.at(0).samples.size() != expected_samples || pick.lights.at(0).sampling_seed != 7 ||
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
    const auto check_pose_and_image = [&](const ColorImage &image)
    {
        const auto pose = session.scene_snapshot().camera;
        if (pose.auto_frame || pose.vertical_fov_degrees != config.camera.vertical_fov_degrees)
        {
            throw std::runtime_error("Navigation changed the explicit camera or its field of view");
        }
        for (const auto delta :
             {pose.position - config.camera.position, pose.target - config.camera.target, pose.up - config.camera.up})
        {
            if (std::hypot(delta.x, delta.y, delta.z) > 1e-12)
            {
                throw std::runtime_error("Viewer orbit differs from the expected pose");
            }
        }
        // Rounding at a nearest-texel boundary can select either texel. Replay the exact displayed pose.
        auto reference = config;
        reference.camera = pose;
        check_image(image, reference);
    };
    check_pose_and_image(latest);

    session.render_settings().mode = RenderMode::Raster;
    try
    {
        session.apply_settings();
        throw std::runtime_error("Accepted rectangle ray shadows in raster mode");
    }
    catch (const std::invalid_argument &)
    {
    }
    if (session.updating())
    {
        throw std::runtime_error("Rejected settings disturbed the settled frame");
    }
    document.set_display_settings({1});
    if (document.update({}) != &latest || document.update({}) || session.updating())
    {
        throw std::runtime_error("Changing exposure must publish the same HDR frame once");
    }

    // Supersede an in-flight navigation frame; the first frame published must already use the new settings.
    (void)session.update({.wheel = 1}, now);
    config.camera.position = config.camera.position / 1.15;
    config.render.extent = {97, 53};
    config.lighting->lights[0].width = 0.4;
    config.lighting->lights[0].shadows = false;
    session.light_settings() = config.lighting;
    session.render_settings() = config.render;
    session.apply_settings();
    // Save before this pose is published, with newer edits still in the panel.
    session.light_settings()->lights[0].width = 9;
    session.render_settings().sampling.samples = 0;
    const auto snapshot = session.scene_snapshot();
    if (snapshot.camera.auto_frame || snapshot.lighting->lights[0].width != config.lighting->lights[0].width ||
        snapshot.render.sampling.samples != config.render.sampling.samples || snapshot.display.exposure_ev != 1)
    {
        throw std::runtime_error("Scene snapshot lost the explicit camera or applied settings");
    }
    const auto &edited = await_frame(session, now);
    check_pose_and_image(edited);
    check_image(edited, snapshot);

    const auto finish_open = [&]
    {
        const ColorImage *image = nullptr;
        while (document.opening_model())
        {
            image = document.update({});
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return image;
    };
    document.open_model(asset / "missing.glb");
    if (finish_open() || document.open_error().empty() || document.display_path() != source ||
        document.session().scene_snapshot().camera.position != snapshot.camera.position)
    {
        throw std::runtime_error("Failed model import replaced the current scene");
    }

    // Open a copy with its geometry intact but its image missing; the published frame and pose must survive.
    const auto scratch = std::filesystem::current_path() / std::format("viewer-gltf-{:08x}", std::random_device{}());
    if (!std::filesystem::create_directory(scratch))
    {
        throw std::runtime_error("Could not create glTF test directory");
    }
    std::filesystem::copy_file(external_model, scratch / external_model.filename());
    std::filesystem::copy(external_model.parent_path() / "buffers", scratch / "buffers",
                          std::filesystem::copy_options::recursive);
    document.open_model(scratch / external_model.filename());
    const auto *replacement = finish_open();
    std::filesystem::remove_all(scratch);
    if (replacement || document.open_error().find("two texels.png") == std::string::npos ||
        document.display_path() != source ||
        document.session().scene_snapshot().camera.position != snapshot.camera.position)
    {
        throw std::runtime_error("Missing external texture did not preserve the active scene");
    }

    document.open_model(external_model);
    session.render_settings().sampling.samples = 4;
    session.render_settings().extent = {43, 67};
    document.apply_settings();                      // This edit must reach the first frame of the new asset.
    session.light_settings()->lights[0].width = -1; // An unapplied draft survives the switch.
    document.set_display_settings({-2});
    const auto *opened = finish_open();
    if (!opened || !document.open_error().empty() || document.display_path() != external_model ||
        document.session().light_settings()->lights[0].width != -1 ||
        document.session().scene_snapshot().display.exposure_ev != -2)
    {
        throw std::runtime_error("Model replacement failed to commit the first frame or preserve the draft");
    }
    config.camera.auto_frame = true;
    config.scene.asset = external_model;
    config.render.sampling.samples = 4;
    config.render.extent = {43, 67};
    config.lighting->lights[0].width = 9; // The earlier unapplied width was accepted by Apply during this load.
    check_image(*opened, config);
}
} // namespace

int main(int argc, char **argv)
try
{
    if (argc != 3)
    {
        return 1;
    }
    check_pan_from_above();
    check_frame_replacement(argv[1], argv[2]);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
