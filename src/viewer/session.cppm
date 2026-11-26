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

template <class T> class PreviewJob
{
  public:
    explicit PreviewJob(std::uint64_t revision, auto work) : revision(revision)
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
    const std::uint64_t revision;

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
    }
    ViewerScene(const ViewerScene &) = delete;
    ViewerScene &operator=(const ViewerScene &) = delete;
    ScenePreview preview;
    PreviewRenderer renderer;
    OrbitCamera orbit;
};

export class ViewerSession
{
  public:
    explicit ViewerSession(RenderRequest request);
    // Consume input against the displayed view before publishing a replacement image.
    [[nodiscard]] const ColorImage *update(const ViewportInput &input);
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
        return requested_ != displayed_;
    }

  private:
    struct Frame
    {
        PreviewView view;
        ColorImage image;
    };
    void select(std::optional<PixelCoord> pixel);
    void navigate(const ViewportInput &input);

    ImageExtent extent_;
    std::unique_ptr<ViewerScene> scene_;
    std::optional<Frame> frame_;
    std::optional<PixelCoord> selection_;
    std::optional<PixelDiagnostic> report_;
    bool focus_pending_{};
    std::uint64_t requested_{1}, displayed_{}, pick_revision_{};
    // Destroy workers before the scene and published frame.
    PreviewJob<std::unique_ptr<ViewerScene>> loading_;
    std::optional<PreviewJob<Frame>> rendering_;
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

void ViewerSession::navigate(const ViewportInput &input)
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
        if (rendering_)
        {
            rendering_->cancel();
        }
    }
}

const ColorImage *ViewerSession::update(const ViewportInput &input)
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
        if (inspecting_->revision == pick_revision_)
        {
            report_ = inspecting_->take();
        }
        inspecting_.reset();
    }
    navigate(input);

    const ColorImage *published = nullptr;
    if (rendering_ && rendering_->ready())
    {
        if (rendering_->revision == requested_)
        {
            frame_ = rendering_->take();
            displayed_ = requested_;
            select(std::nullopt);
            published = &frame_->image;
        }
        rendering_.reset();
    }
    if (updating() && !rendering_)
    {
        rendering_.emplace(requested_, [&renderer = scene_->renderer, view = scene_->preview.view](std::stop_token stop)
                           { return Frame{view, renderer.render(view, stop)}; });
    }
    if (selection_ && !report_ && !inspecting_)
    {
        inspecting_.emplace(pick_revision_, [&renderer = scene_->renderer, view = frame_->view, pixel = *selection_](
                                                std::stop_token stop) { return renderer.diagnose(view, pixel, stop); });
    }
    return published;
}
} // namespace formlight
