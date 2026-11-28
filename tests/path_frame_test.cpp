#include <oneapi/tbb/task_arena.h>

import std;
import formlight.preview;
import formlight.scene_toml;

namespace
{
using namespace formlight;

bool equal(LinearColor a, LinearColor b)
{
    return std::tie(a.r, a.g, a.b, a.a) == std::tie(b.r, b.g, b.b, b.a);
}

void check_batch_rejections(PathFrame &frame)
{
    const auto completed = frame.completed_samples();
    for (const auto count : {0u, std::numeric_limits<std::uint32_t>::max()})
    {
        try
        {
            (void)frame.render_batch(count);
            throw std::runtime_error("Accepted an empty or overflowing sample range");
        }
        catch (const std::invalid_argument &)
        {
        }
    }

    // Interrupt a long batch. The next successful batch must resume at the previous published prefix.
    std::stop_source stop;
    const std::jthread cancel{[&]
                              {
                                  std::this_thread::sleep_for(std::chrono::milliseconds{5});
                                  stop.request_stop();
                              }};
    bool cancelled = false;
    try
    {
        (void)frame.render_batch(65536, stop.get_token());
    }
    catch (const std::runtime_error &error)
    {
        if (std::string_view{error.what()} != "Rendering cancelled")
        {
            throw;
        }
        cancelled = true;
    }
    if (!cancelled || frame.completed_samples() != completed)
    {
        throw std::runtime_error("Interrupted batch advanced the frame");
    }
}

void check_frame(SceneConfig config)
{
    config.render.extent = {33, 19};
    config.render.path_samples = 17;
    const auto request = prepare_render(config);
    const auto preview = load_preview(request);
    const PreviewRenderer renderer{preview.scene, request.lighting, request.mode};
    const auto &view = preview.view;
    const auto reference = oneapi::tbb::task_arena{1}.execute([&] { return renderer.render(view); });

    PathFrame frame{renderer, view};
    oneapi::tbb::task_arena workers{4};
    const auto prefix = workers.execute([&] { return frame.render_batch(3); });
    check_batch_rejections(frame);
    const auto result = workers.execute(
        [&]
        {
            (void)frame.render_batch(4);
            return frame.render_batch(10);
        });
    if (frame.completed_samples() != 17)
    {
        throw std::runtime_error("Expected 17 samples per SSAA location");
    }

    auto prefix_view = view;
    prefix_view.path_sampling.samples = 3;
    bool changed = false;
    for (int y = 0; y < config.render.extent.height; ++y)
    {
        for (int x = 0; x < config.render.extent.width; ++x)
        {
            if (!equal(result[y, x], reference[y, x]) ||
                !equal(prefix[y, x], renderer.diagnose(prefix_view, {x, y}).color))
            {
                throw std::runtime_error(std::format("Batch or diagnostic differs at ({}, {})", x, y));
            }
            changed |= !equal(prefix[y, x], result[y, x]);
        }
    }
    if (!changed)
    {
        throw std::runtime_error("Additional samples did not change any pixel in the stochastic scene");
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
    check_frame(load_scene_config(argv[1]));

    auto final = preset_config(ScenePreset::FinalScene);
    final.camera.lens = {.focus_distance = 800, .defocus_angle_degrees = 0.5};
    final.render.ssaa = 2;
    check_frame(std::move(final));
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
