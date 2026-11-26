module;

#include <SDL3/SDL.h>
#include <imgui.h>

export module formlight.viewer.save_dialog;

import std;
import formlight.scene_toml;
import formlight.viewer.session;

namespace formlight
{
namespace
{
using Path = std::filesystem::path;
using Selection = std::promise<Path>;

std::string path_text(const Path &path)
{
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

// SDL may finish on another thread, even after the viewer has released its future.
void SDLCALL receive_destination(void *userdata, const char *const *files, int) noexcept
{
    std::unique_ptr<Selection> selection{static_cast<Selection *>(userdata)};
    try
    {
        if (!files)
        {
            throw std::runtime_error(SDL_GetError());
        }
        const std::string_view path{*files ? *files : ""};
        selection->set_value(Path{std::u8string{path.begin(), path.end()}});
    }
    catch (...)
    {
        selection->set_exception(std::current_exception());
    }
}
} // namespace

export class SceneSaveDialog
{
  public:
    explicit SceneSaveDialog(const Path &source) : location_(std::filesystem::absolute(source))
    {
    }

    [[nodiscard]] std::string window_title() const
    {
        return "Formlight | " + path_text(location_.filename());
    }

    void draw(SDL_Window *parent, const ViewerSession &session, bool has_image)
    {
        ImGui::BeginDisabled(!has_image || pending_.has_value());
        if (ImGui::Button("Save Scene As..."))
        {
            static constexpr SDL_DialogFileFilter filter{"TOML scene", "toml"};
            const auto initial = path_text(location_);
            auto selection = std::make_unique<Selection>();
            pending_.emplace(session.scene_snapshot(), selection->get_future());
            message_.clear();
            SDL_ShowSaveFileDialog(receive_destination, selection.release(), parent, &filter, 1, initial.c_str());
        }
        ImGui::EndDisabled();
        if (!message_.empty())
        {
            ImGui::TextWrapped("%s", message_.c_str());
        }
    }

    void update(SDL_Window *parent)
    {
        if (!pending_ || pending_->destination.wait_for(std::chrono::seconds{0}) != std::future_status::ready)
        {
            return;
        }
        auto save = std::move(*pending_);
        pending_.reset();
        try
        {
            auto path = save.destination.get();
            if (!path.empty())
            {
                save_scene_config(path, save.config);
                location_ = std::move(path);
                SDL_SetWindowTitle(parent, window_title().c_str());
                message_ = "Saved " + path_text(location_);
            }
        }
        catch (const std::exception &error)
        {
            message_ = std::format("Could not save scene: {}", error.what());
        }
    }

  private:
    struct PendingSave
    {
        SceneConfig config;
        std::future<Path> destination;
    };
    Path location_;
    std::optional<PendingSave> pending_;
    std::string message_;
};
} // namespace formlight
