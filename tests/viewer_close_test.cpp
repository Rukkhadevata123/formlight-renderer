#include <SDL3/SDL.h>

import std;
import formlight.scene_toml;
import formlight.viewer.document;
import formlight.viewer.session;
import formlight.viewer.window;

namespace
{
using namespace formlight;

void close_during_render(const std::filesystem::path &source)
{
    auto config = load_scene_config(source);
    config.render.extent = {81, 61};
    config.render.path_samples = 19;
    ViewerDocument document{source, std::move(config)};
    auto &session = document.session();
    ViewerWindow window{std::move(document)};

    const auto finish_render = [&]
    {
        while (session.updating())
        {
            if (!window.poll())
            {
                throw std::runtime_error("Window closed before the render was ready");
            }
            window.draw();
        }
    };
    finish_render();

    // A full frame at this light budget would outlast the test. Mode changes and close must interrupt sampling.
    const auto start_long_batch = [&]
    {
        session.render_settings().mode = RenderMode::Path;
        session.render_settings().sampling.samples = 1000000;
        session.apply_settings();
        window.draw();
    };
    start_long_batch();
    session.render_settings().mode = RenderMode::Hybrid;
    session.render_settings().sampling.samples = 4;
    session.apply_settings();
    finish_render();
    start_long_batch();

    int count{};
    const std::unique_ptr<SDL_Window *, decltype(&SDL_free)> windows{SDL_GetWindows(&count), SDL_free};
    if (!windows || count != 1 || !session.updating())
    {
        throw std::runtime_error("Expected an active render in one SDL window");
    }
    SDL_Event close{};
    close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    close.window.windowID = SDL_GetWindowID(*windows);
    if (!SDL_PushEvent(&close) || window.poll())
    {
        throw std::runtime_error("Viewer ignored the SDL window close event");
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
    close_during_render(argv[1]);
    if (SDL_WasInit(SDL_INIT_VIDEO) != 0)
    {
        throw std::runtime_error("SDL video is still active after the viewer closed");
    }
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
