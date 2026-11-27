export module formlight.viewer.session;

import std;
import formlight.preview;
import formlight.viewer.orbit_camera;

namespace formlight
{
export struct ViewportInput
{
    ImagePoint orbit_pixels{};
    ImagePoint pan_heights{};
    double wheel{};
    bool focus{};
    std::optional<PixelCoord> picked{};
};

template <class T, class Request = std::uint64_t> class PreviewJob
{
  public:
    explicit PreviewJob(Request request, auto work) : request(std::move(request))
    {
        std::packaged_task<T(std::stop_token)> task{std::move(work)};
        result_ = task.get_future();
        worker_ = std::jthread{std::move(task)};
    }
    [[nodiscard]] bool ready() const
    {
        return result_.valid() && result_.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
    }
    T take()
    {
        return result_.get();
    }
    void cancel()
    {
        worker_.request_stop();
    }
    const Request request;

  private:
    std::future<T> result_;
    std::jthread worker_;
};

// Render and inspection jobs borrow the mesh and texture storage at this stable address.
struct ViewerScene
{
    explicit ViewerScene(const RenderRequest &request) : preview(load_preview(request)), orbit(preview.view.camera)
    {
    }
    ViewerScene(const ViewerScene &) = delete;
    ViewerScene &operator=(const ViewerScene &) = delete;
    ScenePreview preview;
    OrbitCamera orbit;

    void resize(ImageExtent extent, bool auto_frame)
    {
        auto &camera = preview.view.camera;
        if (camera.extent().width == extent.width && camera.extent().height == extent.height)
        {
            return;
        }
        if (auto_frame)
        {
            preview.view = frame_bounds(preview.bounds, preview.subject, extent, camera.view().vertical_fov_radians);
            orbit = OrbitCamera{preview.view.camera};
            return;
        }
        camera = PinholeCamera{camera.view(), extent};
        orbit.resize(extent);
    }
};

export class ViewerSession
{
  public:
    using Clock = std::chrono::steady_clock;
    explicit ViewerSession(SceneConfig config);
    [[nodiscard]] const SceneConfig &applied_config() const
    {
        return applied_.config;
    }
    [[nodiscard]] RenderSettings &render_settings()
    {
        return draft_.render;
    }
    [[nodiscard]] std::optional<LightingSettings> &light_settings()
    {
        return draft_.lighting;
    }
    void apply_settings();
    void set_display_settings(DisplaySettings display);
    [[nodiscard]] SceneConfig scene_snapshot() const;
    void check_output_path(const std::filesystem::path &path) const
    {
        if (scene_)
        {
            scene_->preview.scene.check_output_path(path);
        }
    }
    // Consume input against the displayed view before publishing a replacement image.
    [[nodiscard]] const ColorImage *update(const ViewportInput &input, Clock::time_point now = Clock::now());
    [[nodiscard]] ImageExtent display_extent() const
    {
        return frame_ ? frame_->request.full_extent : applied_.request.extent;
    }
    [[nodiscard]] auto selection() const
    {
        return selection_;
    }
    [[nodiscard]] const std::optional<PixelDiagnostic> &report() const
    {
        return report_;
    }
    [[nodiscard]] bool updating() const
    {
        return !frame_ || frame_->request.revision != requested_ || showing_preview();
    }
    [[nodiscard]] bool showing_preview() const
    {
        return frame_ && frame_->request.quality != Quality::Full;
    }

  private:
    enum class Quality
    {
        Full,
        LightCenter,
        HalfResolution
    };
    struct FrameRequest
    {
        std::uint64_t revision;
        Quality quality;
        bool navigation;
        ImageExtent full_extent;
        PreviewView view;
    };
    struct TimedImage
    {
        ColorImage image;
        Clock::duration elapsed;
        PreviewRenderer renderer;
    };
    struct Frame
    {
        FrameRequest request;
        TimedImage result;
    };
    void select(std::optional<PixelCoord> pixel);
    void navigate(const ViewportInput &input, Clock::time_point now);
    void schedule_render(Clock::time_point now);
    [[nodiscard]] Quality navigation_quality(Clock::duration budget) const;

    struct AppliedSettings
    {
        SceneConfig config;
        RenderRequest request{prepare_render(config)};
    };
    SceneConfig draft_;
    AppliedSettings applied_;
    std::unique_ptr<ViewerScene> scene_;
    std::optional<Frame> frame_;
    bool redisplay_{};
    std::optional<PixelCoord> selection_;
    std::optional<PixelDiagnostic> report_;
    bool focus_pending_{};
    std::uint64_t requested_{1}, settings_revision_{1}, pick_revision_{};
    Clock::time_point refine_after_{Clock::time_point::min()};
    Quality gesture_quality_{Quality::Full};
    Clock::duration full_cost_{}, light_center_cost_{};
    // Destroy workers before the scene and published frame.
    PreviewJob<std::unique_ptr<ViewerScene>> loading_;
    std::optional<PreviewJob<TimedImage, FrameRequest>> rendering_;
    std::optional<PreviewJob<PixelDiagnostic>> inspecting_;
};

ViewerSession::ViewerSession(SceneConfig config)
    : draft_(std::move(config)), applied_{draft_},
      loading_(0, [request = applied_.request](std::stop_token) { return std::make_unique<ViewerScene>(request); })
{
}

void ViewerSession::apply_settings()
{
    AppliedSettings next{draft_};
    if (scene_)
    {
        scene_->resize(next.request.extent, !frame_ && next.config.camera.auto_frame);
    }
    applied_ = std::move(next);
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

void ViewerSession::set_display_settings(DisplaySettings display)
{
    display.validate();
    applied_.config.display = display;
    draft_.display = display;
    redisplay_ = true;
}

SceneConfig ViewerSession::scene_snapshot() const
{
    if (!scene_)
    {
        throw std::logic_error("The scene camera is still loading");
    }
    auto snapshot = applied_.config;
    const auto camera = scene_->preview.view.camera.view();
    // Navigation changes the pose but preserves the authored vertical FOV in degrees.
    snapshot.camera.auto_frame = false;
    snapshot.camera.position = camera.position;
    snapshot.camera.target = camera.target;
    snapshot.camera.up = camera.up;
    return snapshot;
}

void ViewerSession::select(std::optional<PixelCoord> pixel)
{
    ++pick_revision_;
    selection_ = pixel;
    report_.reset();
    focus_pending_ = false;
    if (inspecting_)
    {
        inspecting_->cancel();
    }
}

ViewerSession::Quality ViewerSession::navigation_quality(Clock::duration budget) const
{
    if (full_cost_ <= budget)
    {
        return Quality::Full;
    }
    const auto &lighting = applied_.request.lighting;
    const auto multisampled = [](const ActiveLight &light)
    {
        const auto *area = std::get_if<AreaLighting>(&light.source);
        return area && area->sampling.samples > 1;
    };
    if (lighting && light_center_cost_ <= budget && std::ranges::any_of(lighting->direct, multisampled))
    {
        return Quality::LightCenter;
    }
    return Quality::HalfResolution;
}

void ViewerSession::navigate(const ViewportInput &input, Clock::time_point now)
{
    auto &camera = scene_->orbit;
    bool changed = false;
    if (focus_pending_ && report_)
    {
        focus_pending_ = false;
        // Focus the nearest visible sample when the selected pixel straddles a silhouette.
        const auto closest = std::ranges::min_element(
            report_->samples, {}, [](const SampleDiagnostic &sample)
            { return sample.surface ? sample.surface->hit.view_depth : std::numeric_limits<double>::infinity(); });
        if (closest != report_->samples.end() && closest->surface)
        {
            const auto &instance = scene_->preview.scene.instance(closest->surface->hit.surface.id.instance);
            camera.focus(world_bounds(std::span{&instance, 1}));
            changed = true;
        }
    }
    if (input.orbit_pixels.x != 0 || input.orbit_pixels.y != 0)
    {
        camera.orbit(input.orbit_pixels.x, input.orbit_pixels.y);
        changed = true;
    }
    if (input.pan_heights.x != 0 || input.pan_heights.y != 0)
    {
        camera.pan(input.pan_heights.x, input.pan_heights.y);
        changed = true;
    }
    if (input.wheel != 0)
    {
        camera.zoom(input.wheel);
        changed = true;
    }
    if (changed)
    {
        scene_->preview.view = camera.view(scene_->preview.bounds);
        ++requested_;
        // Start with headroom for presentation. During a drag, only downgrade on sustained slow frames.
        if (now >= refine_after_)
        {
            gesture_quality_ = navigation_quality(std::chrono::milliseconds{25});
        }
        else
        {
            gesture_quality_ = std::max(gesture_quality_, navigation_quality(std::chrono::milliseconds{40}));
        }
        refine_after_ = now + std::chrono::milliseconds{120};
        if (rendering_ && !rendering_->request.navigation)
        {
            rendering_->cancel();
        }
    }
}

void ViewerSession::schedule_render(Clock::time_point now)
{
    if (rendering_ || !updating())
    {
        return;
    }
    auto view = scene_->preview.view;
    view.ssaa = applied_.request.ssaa;
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
    auto renderer = frame_ ? frame_->result.renderer : PreviewRenderer{scene_->preview.scene};
    rendering_.emplace(
        FrameRequest{requested_, quality, navigating, applied_.request.extent, view},
        [renderer = std::move(renderer), lighting = applied_.request.lighting, view](std::stop_token stop) mutable
        {
            renderer.set_lighting(std::move(lighting));
            const auto start = Clock::now();
            auto image = renderer.render(view, stop);
            return TimedImage{std::move(image), Clock::now() - start, std::move(renderer)};
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
        // Even a full-quality navigation frame may finish while more input accumulates.
        const auto &request = rendering_->request;
        if (request.revision == requested_ || (request.navigation && request.revision >= settings_revision_))
        {
            auto result = rendering_->take();
            // A half-size preview has about a quarter of the native pixel count.
            const auto estimate = request.quality == Quality::HalfResolution ? 4 * result.elapsed : result.elapsed;
            auto &cost = request.quality == Quality::Full ? full_cost_ : light_center_cost_;
            cost = cost == Clock::duration::zero() ? estimate : cost + (estimate - cost) / 4;
            frame_.emplace(request, std::move(result));
            select(std::nullopt);
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
