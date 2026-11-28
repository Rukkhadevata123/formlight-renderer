import std;
import formlight.preview;
import formlight.viewer.session;

namespace
{
using namespace formlight;

const ColorImage &await_image(ViewerSession &session, ViewerSession::Clock::time_point now)
{
    for (;;)
    {
        if (const auto *image = session.update({}, now))
        {
            return *image;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
}

void check_prefix(const ViewerSession &session, const ColorImage &actual, const PreviewRenderer &offline,
                  PreviewView view, std::uint32_t completed)
{
    const auto progress = session.path_progress().value();
    if (progress.completed != completed || progress.target != view.path_sampling.samples ||
        session.updating() != (completed < progress.target))
    {
        throw std::runtime_error(std::format("Incorrect viewer progress at {} samples", completed));
    }
    view.path_sampling.samples = completed;
    const auto reference = offline.render(view);
    const auto [width, height] = reference.extent();
    if (actual.extent().width != width || actual.extent().height != height)
    {
        throw std::runtime_error("Path frame has the wrong extent");
    }
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto a = actual[y, x], b = reference[y, x];
            if (std::tie(a.r, a.g, a.b, a.a) != std::tie(b.r, b.g, b.b, b.a))
            {
                throw std::runtime_error(
                    std::format("Viewer and offline RGBA differ at ({}, {}), {} samples", x, y, completed));
            }
        }
    }
}

void check_pick(ViewerSession &session, ViewerSession::Clock::time_point now, LinearColor displayed,
                std::size_t expected_rays)
{
    if (!session.selection())
    {
        throw std::runtime_error("Lost the selected pixel");
    }
    while (!session.report())
    {
        (void)session.update({}, now);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    const auto picked = session.report()->color;
    if (session.report()->samples.size() != expected_rays ||
        std::tie(picked.r, picked.g, picked.b) != std::tie(displayed.r, displayed.g, displayed.b))
    {
        throw std::runtime_error("Pixel inspection did not replay the displayed path prefix");
    }
}
} // namespace

int main()
try
{
    auto config = preset_config(ScenePreset::FinalScene);
    config.render.extent = {33, 23};
    config.render.ssaa = 2;
    config.render.path_samples = 19;
    const auto request = prepare_render(config);
    const auto preview = load_preview(request);
    const PreviewRenderer offline{preview.scene, request.lighting, request.mode};
    auto view = preview.view;
    ViewerSession session{config};
    const auto now = ViewerSession::Clock::now();
    check_prefix(session, await_image(session, now), offline, view, 8);

    // Replace an in-flight prefix. A late result from the old seed must never reach the screen.
    view.path_sampling.seed = 23;
    session.render_settings().sampling.seed = view.path_sampling.seed;
    session.apply_settings();
    check_prefix(session, await_image(session, now), offline, view, 8);
    session.set_display_settings({1});
    constexpr PixelCoord pixel{16, 11};
    const auto *second = session.update({.picked = pixel}, now);
    if (!second || session.path_progress()->completed == 8)
    {
        // Exposure may redisplay the current prefix before the next batch finishes.
        second = &await_image(session, now);
    }
    check_prefix(session, *second, offline, view, 16);
    const auto &final = await_image(session, now);
    check_prefix(session, final, offline, view, 19);
    check_pick(session, now, final[pixel.y, pixel.x], 4 * 19);

    // Issue a second pose before polling the first navigation job, even if that job has already finished.
    (void)session.update({.orbit_pixels = {12, 4}}, now);
    if (session.update({.orbit_pixels = {-3, 5}}, now))
    {
        throw std::runtime_error("A superseded path navigation frame was published");
    }
    // Keep the navigation clock within the refinement delay to inspect the first batch at this pose.
    const auto &partial = await_image(session, now);
    if (session.path_progress()->completed != 8 || session.selection())
    {
        throw std::runtime_error("The new pose inherited samples or a selection");
    }
    const PixelCoord center{partial.extent().width / 2, partial.extent().height / 2};
    (void)session.update({.picked = center}, now);
    check_pick(session, now, partial[center.y, center.x], 4 * 8);

    const auto settled = now + std::chrono::seconds{1};
    const ColorImage *moved = nullptr;
    while (session.updating())
    {
        moved = &await_image(session, settled);
    }
    if (!moved)
    {
        throw std::runtime_error("New pose did not replace the selected frame");
    }
    const auto new_view = load_preview(prepare_render(session.scene_snapshot())).view;
    check_prefix(session, *moved, offline, new_view, 19);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
