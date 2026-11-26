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

// Lives at a stable address for the render and inspection jobs that borrow its renderer.
struct ViewerScene
{
    explicit ViewerScene(const RenderRequest &request)
        : preview(load_preview(request)), renderer(preview.scene, request.lighting), orbit(preview.view.camera)
    {
        const auto *area = request.lighting ? std::get_if<AreaLighting>(&*request.lighting) : nullptr;
        can_reduce_samples = area && area->sampling.samples > 1;
    }
    ViewerScene(const ViewerScene &) = delete;
    ViewerScene &operator=(const ViewerScene &) = delete;
    ScenePreview preview;
    PreviewRenderer renderer;
    OrbitCamera orbit;
    bool can_reduce_samples{};
};

export class ViewerSession
{
  public:
    using Clock = std::chrono::steady_clock;
    explicit ViewerSession(RenderRequest request);
    // Consume input against the displayed view before publishing a replacement image.
    [[nodiscard]] const ColorImage *update(const ViewportInput &input, Clock::time_point now = Clock::now());
    [[nodiscard]] ImageExtent extent() const
    {
        return extent_;
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
        PreviewView view;
    };
    struct TimedImage
    {
        ColorImage image;
        Clock::duration elapsed;
    };
    struct Frame
    {
        FrameRequest request;
        ColorImage image;
    };
    void select(std::optional<PixelCoord> pixel);
    void navigate(const ViewportInput &input, Clock::time_point now);
    void schedule_render(Clock::time_point now);
    [[nodiscard]] Quality navigation_quality(Clock::duration budget) const;

    ImageExtent extent_;
    std::unique_ptr<ViewerScene> scene_;
    std::optional<Frame> frame_;
    std::optional<PixelCoord> selection_;
    std::optional<PixelDiagnostic> report_;
    bool focus_pending_{};
    std::uint64_t requested_{1}, pick_revision_{};
    Clock::time_point refine_after_{Clock::time_point::min()};
    Quality gesture_quality_{Quality::Full};
    Clock::duration full_cost_{}, light_center_cost_{};
    // Destroy workers before the scene and published frame.
    PreviewJob<std::unique_ptr<ViewerScene>> loading_;
    std::optional<PreviewJob<TimedImage, FrameRequest>> rendering_;
    std::optional<PreviewJob<PixelDiagnostic>> inspecting_;
};

ViewerSession::ViewerSession(RenderRequest request)
    : extent_(request.extent),
      loading_(0, [request = std::move(request)](std::stop_token) { return std::make_unique<ViewerScene>(request); })
{
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
    if (scene_->can_reduce_samples && light_center_cost_ <= budget)
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
        if (report_->surface)
        {
            const auto &instance = scene_->preview.scene.instance(report_->surface->hit.surface.id.instance);
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
    const bool navigating = frame_ && frame_->request.revision != requested_;
    if (!navigating && now < refine_after_)
    {
        return;
    }
    const Quality quality = navigating ? gesture_quality_ : Quality::Full;
    if (quality == Quality::HalfResolution)
    {
        // Round up so odd extents retain their last row/column and a one-pixel axis stays valid.
        const ImageExtent half{extent_.width / 2 + extent_.width % 2, extent_.height / 2 + extent_.height % 2};
        view.camera = PinholeCamera{view.camera.view(), half};
    }
    if (quality != Quality::Full)
    {
        view.area_light_pattern = AreaLightPattern::Center;
    }
    rendering_.emplace(FrameRequest{requested_, quality, navigating, view},
                       [&renderer = scene_->renderer, view](std::stop_token stop)
                       {
                           const auto start = Clock::now();
                           auto image = renderer.render(view, stop);
                           return TimedImage{std::move(image), Clock::now() - start};
                       });
}

const ColorImage *ViewerSession::update(const ViewportInput &input, Clock::time_point now)
{
    if (loading_.ready())
    {
        scene_ = loading_.take();
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
    if (rendering_ && rendering_->ready())
    {
        // Even a full-quality navigation frame may finish while more input accumulates.
        const auto &request = rendering_->request;
        if (request.navigation || request.revision == requested_)
        {
            auto [image, elapsed] = rendering_->take();
            // A half-size preview has about a quarter of the native pixel count.
            const auto estimate = request.quality == Quality::HalfResolution ? 4 * elapsed : elapsed;
            auto &cost = request.quality == Quality::Full ? full_cost_ : light_center_cost_;
            cost = frame_ ? cost + (estimate - cost) / 4 : estimate;
            frame_ = Frame{request, std::move(image)};
            select(std::nullopt);
            published = &frame_->image;
        }
        rendering_.reset();
    }
    schedule_render(now);
    if (selection_ && !report_ && !inspecting_)
    {
        inspecting_.emplace(pick_revision_, [&renderer = scene_->renderer, view = frame_->request.view,
                                             pixel = *selection_](std::stop_token stop)
                            { return renderer.diagnose(view, pixel, stop); });
    }
    return published;
}
} // namespace formlight
