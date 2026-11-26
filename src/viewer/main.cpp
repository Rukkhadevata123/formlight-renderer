import std;
import formlight.preview;
import formlight.scene_toml;
import formlight.viewer.window;

namespace
{
using namespace formlight;

// The jthread requests cancellation and joins before the future or any borrowed scene is destroyed.
template <class T> class PreviewJob
{
  public:
    explicit PreviewJob(auto work)
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

  private:
    std::future<T> result_;
    std::jthread worker_;
};

struct Frame
{
    ScenePreview preview;
    ColorImage image;
};

void view_scene(const std::filesystem::path &path)
{
    const auto request = prepare_render(load_scene_config(path));
    const auto title = std::format("Formlight | {}", path.filename().string());
    ViewerWindow window{title.c_str()};
    PreviewJob<Frame> rendering{[&](std::stop_token stop)
                                {
                                    auto preview = load_preview(request);
                                    auto image = render_preview(preview.scene, preview.view, request.lighting, stop);
                                    return Frame{std::move(preview), std::move(image)};
                                }};
    std::optional<Frame> frame;
    std::optional<PixelDiagnostic> report;
    std::optional<PixelCoord> selection;
    std::optional<PreviewJob<PixelDiagnostic>> inspecting;
    while (window.poll())
    {
        if (rendering.ready())
        {
            frame.emplace(rendering.take());
            window.upload(frame->image);
        }
        if (inspecting && inspecting->ready())
        {
            auto result = inspecting->take();
            inspecting.reset();
            if (result.pixel.x == selection->x && result.pixel.y == selection->y)
            {
                report = std::move(result);
            }
        }
        if (auto clicked = window.draw(report, selection))
        {
            selection = clicked;
            report.reset();
        }
        if (frame && selection && !report && !inspecting)
        {
            inspecting.emplace(
                [&, pixel = *selection](std::stop_token stop)
                { return diagnose_pixel(frame->preview.scene, frame->preview.view, pixel, request.lighting, stop); });
        }
    }
}
} // namespace

int main(int argc, char **argv)
{
    if (argc == 1 || (argc == 2 && std::string_view{argv[1]} == "--help"))
    {
        std::println("Usage: formlight-viewer --scene SCENE.toml");
        return 0;
    }
    if (argc != 3 || std::string_view{argv[1]} != "--scene")
    {
        std::cerr << "Expected --scene SCENE.toml\n";
        return 1;
    }
    try
    {
        view_scene(argv[2]);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
