module formlight.viewer.session;

import std;
import formlight.preview;

namespace formlight
{
namespace
{
bool can_extend_paths(const SceneConfig &current, const SceneConfig &draft)
{
    if (current.render.mode != RenderMode::Path || draft.render.path_samples < current.render.path_samples)
    {
        return false;
    }
    // Scene assets belong to the session; navigation invalidates the camera revision independently of Apply.
    auto render = current.render;
    render.path_samples = draft.render.path_samples;
    return render == draft.render && current.camera == draft.camera && current.lighting == draft.lighting;
}
} // namespace

ViewerSession::~ViewerSession()
{
    // Signal both workers before either jthread joins. Their borrowed scene is destroyed last.
    if (rendering_)
    {
        rendering_->cancel();
    }
    if (inspecting_)
    {
        inspecting_->cancel();
    }
}

void ViewerSession::cancel_path_render()
{
    if (applied_.request.mode == RenderMode::Path && updating())
    {
        path_cancelled_ = true;
        if (rendering_)
        {
            rendering_->cancel();
        }
    }
}

void ViewerSession::apply_settings()
{
    if (can_extend_paths(applied_.config, draft_))
    {
        const auto target = draft_.render.path_samples;
        if (target > applied_.config.render.path_samples)
        {
            resume_path_render();
        }
        applied_.config.render.path_samples = target;
        applied_.request.path_sampling.samples = target;
        if (frame_ && frame_->request.revision == requested_)
        {
            frame_->path_target = target;
        }
        return;
    }

    AppliedSettings next{draft_};
    if (scene_)
    {
        scene_->resize(next.request.extent, !frame_ && next.config.camera.auto_frame);
    }
    applied_ = std::move(next);
    path_cancelled_ = false;
    settings_revision_ = ++requested_;
    select(std::nullopt);
    if (rendering_)
    {
        rendering_->cancel();
    }
    refine_after_ = Clock::time_point::min();
    gesture_quality_ = Quality::Full;
    full_cost_ = light_center_cost_ = {};
}

void ViewerSession::schedule_render(Clock::time_point now)
{
    if (rendering_ || !updating())
    {
        return;
    }
    auto view = scene_->preview.view;
    view.ssaa = applied_.request.ssaa;
    view.path_sampling = applied_.request.path_sampling;
    view.lens = applied_.request.lens;
    view.shutter = applied_.request.shutter;
    const bool navigating = frame_ && frame_->request.revision != requested_ && requested_ > settings_revision_;
    if (!navigating && now < refine_after_)
    {
        return;
    }
    const Quality quality = navigating ? gesture_quality_ : Quality::Full;
    if (quality == Quality::HalfResolution)
    {
        // Round up so odd extents retain their last row/column and a one-pixel axis stays valid.
        const auto [width, height] = applied_.request.extent;
        const ImageExtent half{width / 2 + width % 2, height / 2 + height % 2};
        view.camera = PinholeCamera{view.camera.view(), half};
    }
    if (quality != Quality::Full)
    {
        view.area_light_pattern = AreaLightPattern::Center;
    }
    auto previous = frame_ ? std::optional{frame_->result.renderer} : std::nullopt;
    std::optional<PathFrame> accumulation;
    const auto mode = applied_.request.mode;
    std::uint32_t batch{};
    if (mode == RenderMode::Path)
    {
        if (frame_ && frame_->request.revision == requested_ && frame_->request.quality == quality)
        {
            // Keep the displayed prefix available if the user cancels before this batch is published.
            accumulation.emplace(*frame_->result.accumulation);
        }
        constexpr std::uint32_t samples_per_batch = 8;
        const auto completed = accumulation ? accumulation->completed_samples() : 0u;
        batch = std::min(samples_per_batch, view.path_sampling.samples - completed);
        // The published view also drives pixel inspection, which replays exactly this prefix.
        view.path_sampling.samples = completed + batch;
    }
    rendering_.emplace(
        FrameRequest{requested_, quality, navigating, applied_.request.extent, view, mode},
        [previous = std::move(previous), accumulation = std::move(accumulation), batch, &scene = scene_->preview.scene,
         lighting = applied_.request.lighting, mode, view](std::stop_token stop) mutable
        {
            if (!previous)
            {
                previous.emplace(scene, std::move(lighting), mode);
            }
            else if (!accumulation)
            {
                previous->configure(mode, std::move(lighting));
            }
            const auto start = Clock::now();
            if (mode == RenderMode::Path && !accumulation)
            {
                accumulation.emplace(*previous, view);
            }
            auto image = accumulation ? accumulation->render_batch(batch, stop) : previous->render(view, stop);
            return TimedImage{std::move(image), Clock::now() - start, std::move(*previous), std::move(accumulation)};
        });
}

const ColorImage *ViewerSession::update(const ViewportInput &input, Clock::time_point now)
{
    if (loading_.ready())
    {
        scene_ = loading_.take();
        scene_->resize(applied_.request.extent, applied_.config.camera.auto_frame);
    }
    if (!scene_)
    {
        return nullptr;
    }

    if (frame_ && input.picked)
    {
        select(input.picked);
    }
    if (input.focus && selection_)
    {
        focus_pending_ = true;
    }
    if (inspecting_ && inspecting_->ready())
    {
        if (inspecting_->request == pick_revision_)
        {
            report_ = inspecting_->take();
        }
        inspecting_.reset();
    }
    navigate(input, now);

    const ColorImage *published = nullptr;
    if (std::exchange(redisplay_, false) && frame_)
    {
        published = &frame_->result.image;
    }
    if (rendering_ && rendering_->ready())
    {
        const auto &request = rendering_->request;
        // Raster navigation can present during a drag. A path prefix belongs to one exact camera pose.
        const bool raster_navigation =
            request.mode != RenderMode::Path && request.navigation && request.revision >= settings_revision_;
        if (!rendering_->stop_requested() && (request.revision == requested_ || raster_navigation))
        {
            auto result = rendering_->take();
            // A half-size preview has about a quarter of the native pixel count.
            const auto estimate = request.quality == Quality::HalfResolution ? 4 * result.elapsed : result.elapsed;
            auto &cost = request.quality == Quality::Full ? full_cost_ : light_center_cost_;
            cost = cost == Clock::duration::zero() ? estimate : cost + (estimate - cost) / 4;
            const bool next_batch = request.mode == RenderMode::Path && frame_ &&
                                    request.revision == frame_->request.revision &&
                                    request.quality == frame_->request.quality;
            // The worker reports its original prefix. Apply may have raised the goal since it started.
            const auto path_target = request.mode == RenderMode::Path ? applied_.request.path_sampling.samples : 0u;
            frame_.emplace(request, std::move(result), path_target);
            if (!next_batch)
            {
                select(std::nullopt);
            }
            else if (selection_)
            {
                invalidate_report();
            }
            published = &frame_->result.image;
        }
        rendering_.reset();
    }
    schedule_render(now);
    if (selection_ && !report_ && !inspecting_)
    {
        inspecting_.emplace(pick_revision_, [renderer = frame_->result.renderer, view = frame_->request.view,
                                             pixel = *selection_](std::stop_token stop)
                            { return renderer.diagnose(view, pixel, stop); });
    }
    return published;
}
} // namespace formlight
