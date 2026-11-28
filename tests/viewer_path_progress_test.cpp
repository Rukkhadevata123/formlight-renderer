#include "viewer_wait.hpp"

import std;
import formlight.preview;
import formlight.viewer.session;

namespace
{
using namespace formlight;
using test::await_frame;

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
    const auto &report = test::await_report(session, now);
    const auto picked = report.color;
    if (report.samples.size() != expected_rays ||
        std::tie(picked.r, picked.g, picked.b) != std::tie(displayed.r, displayed.g, displayed.b))
    {
        throw std::runtime_error("Pixel inspection did not replay the displayed path prefix");
    }
}

void check_sample_target_edits(ViewerSession &session, ViewerSession::Clock::time_point now, const ColorImage &original,
                               const PreviewRenderer &offline, PreviewView view)
{
    const auto apply_target = [&](std::uint32_t target)
    {
        session.render_settings().path_samples = target;
        session.apply_settings();
        view.path_sampling.samples = target;
        if (session.scene_snapshot().render.path_samples != target)
        {
            throw std::runtime_error("Scene snapshot lost the applied path sample target");
        }
    };

    // Keep the displayed 19-sample image until its first additional batch is ready.
    apply_target(35);
    check_prefix(session, original, offline, view, 19);
    check_prefix(session, await_frame(session, now), offline, view, 27);
    check_prefix(session, await_frame(session, now), offline, view, 35);

    // The short 36..38 batch is already scheduled when the second increase arrives.
    apply_target(38);
    (void)session.update({}, now);
    apply_target(51);
    check_prefix(session, await_frame(session, now), offline, view, 38);
    check_prefix(session, await_frame(session, now), offline, view, 46);
    check_prefix(session, await_frame(session, now), offline, view, 51);

    session.shutter_settings() = {0.5, 0.5};
    view.shutter = session.shutter_settings();
    apply_target(57);
    check_prefix(session, await_frame(session, now), offline, view, 8);

    apply_target(5);
    check_prefix(session, await_frame(session, now), offline, view, 5);

    session.light_settings().emplace().lights = {
        {.type = LightKind::Rectangle, .center = {275, 553, 275}, .width = 200, .height = 200, .power = 100000}};
    apply_target(13);
    auto relit = offline;
    relit.set_lighting(prepare_render(session.scene_snapshot()).lighting);
    check_prefix(session, await_frame(session, now), relit, view, 8);
    check_prefix(session, await_frame(session, now), relit, view, 13);
    apply_target(21);
    check_prefix(session, await_frame(session, now), relit, view, 21);
}

void check_cancellation(SceneConfig config, const PreviewRenderer &offline, PreviewView view)
{
    config.render.path_samples = 11;
    view.path_sampling.samples = 11;
    ViewerSession session{std::move(config)};
    const auto now = ViewerSession::Clock::now();
    session.cancel_path_render();
    if (!session.path_cancelled() || session.updating() || session.update({}, now) || session.path_progress())
    {
        throw std::runtime_error("Cancelling before the first batch should leave the viewport empty");
    }
    session.resume_path_render();
    const auto &first = await_frame(session, now);
    check_prefix(session, first, offline, view, 8);

    // The 9..11 batch is already scheduled and may even be ready. Freeze the displayed 8-sample image.
    session.cancel_path_render();
    session.set_display_settings({1});
    constexpr PixelCoord pixel{16, 11};
    if (session.update({.picked = pixel}, now) != &first || session.updating() || !session.path_cancelled())
    {
        throw std::runtime_error("Cancelled frame was replaced or exposure restarted sampling");
    }
    check_pick(session, now, first[pixel.y, pixel.x], 4 * 8);
    if (session.path_progress()->completed != 8 || session.update({}, now))
    {
        throw std::runtime_error("A cancelled batch reached the viewport");
    }

    // A larger target resumes from 8. Publishing the discarded tail would incorrectly produce 11 here.
    view.path_sampling.samples = 19;
    session.render_settings().path_samples = 19;
    session.apply_settings();
    check_prefix(session, await_frame(session, now), offline, view, 16);
    session.cancel_path_render();
    session.resume_path_render();
    check_prefix(session, await_frame(session, now), offline, view, 19);
    session.cancel_path_render();
    if (session.path_cancelled() || session.updating())
    {
        throw std::runtime_error("Cancelling a finished frame changed its state");
    }

    session.render_settings().path_samples = 27;
    session.apply_settings();
    session.cancel_path_render();
    (void)session.update({.wheel = 1}, now);
    (void)await_frame(session, now);
    if (session.path_cancelled() || session.path_progress()->completed != 8 || session.selection())
    {
        throw std::runtime_error("Navigation after cancellation did not start a fresh view");
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
    check_prefix(session, await_frame(session, now), offline, view, 8);

    // Replace an in-flight prefix. A late result from the old seed must never reach the screen.
    view.path_sampling.seed = 23;
    session.render_settings().sampling.seed = view.path_sampling.seed;
    session.apply_settings();
    check_prefix(session, await_frame(session, now), offline, view, 8);
    session.set_display_settings({1});
    constexpr PixelCoord pixel{16, 11};
    const auto *second = session.update({.picked = pixel}, now);
    if (!second || session.path_progress()->completed == 8)
    {
        // Exposure may redisplay the current prefix before the next batch finishes.
        second = &await_frame(session, now);
    }
    check_prefix(session, *second, offline, view, 16);
    const auto &final = await_frame(session, now);
    check_prefix(session, final, offline, view, 19);
    check_pick(session, now, final[pixel.y, pixel.x], 4 * 19);

    // Issue a second pose before polling the first navigation job, even if that job has already finished.
    (void)session.update({.orbit_pixels = {12, 4}}, now);
    if (session.update({.orbit_pixels = {-3, 5}}, now))
    {
        throw std::runtime_error("A superseded path navigation frame was published");
    }
    // Keep the navigation clock within the refinement delay to inspect the first batch at this pose.
    const auto &partial = await_frame(session, now);
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
        moved = &await_frame(session, settled);
    }
    if (!moved)
    {
        throw std::runtime_error("New pose did not replace the selected frame");
    }
    const auto new_view = load_preview(prepare_render(session.scene_snapshot())).view;
    check_prefix(session, *moved, offline, new_view, 19);
    check_sample_target_edits(session, settled, *moved, offline, new_view);
    check_cancellation(config, offline, preview.view);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
