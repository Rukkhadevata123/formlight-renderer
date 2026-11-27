module;

#include <SDL3/SDL.h>
#include <imgui.h>

export module formlight.viewer.file_dialogs;

import std;
import formlight.scene_toml;
import formlight.viewer.document;

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
void SDLCALL receive_selection(void *userdata, const char *const *files, int) noexcept
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

export class SceneFileDialogs
{
  public:
    [[nodiscard]] static std::string window_title(const ViewerDocument &document)
    {
        return "Formlight | " + path_text(document.display_path().filename());
    }

    void draw(SDL_Window *parent, ViewerDocument &document, bool has_image)
    {
        ImGui::BeginDisabled(!has_image || pending_.has_value() || document.opening_model());
        if (ImGui::Button("Open Model..."))
        {
            static constexpr SDL_DialogFileFilter filter{"glTF model", "gltf;glb"};
            const auto initial = path_text(document.session().applied_config().scene.asset.parent_path());
            auto selection = std::make_unique<Selection>();
            pending_.emplace(selection->get_future(), std::nullopt);
            message_.clear();
            SDL_ShowOpenFileDialog(receive_selection, selection.release(), parent, &filter, 1, initial.c_str(), false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Scene As..."))
        {
            static constexpr SDL_DialogFileFilter filter{"TOML scene", "toml"};
            const auto initial = path_text(document.save_location());
            auto selection = std::make_unique<Selection>();
            pending_.emplace(selection->get_future(), document.session().scene_snapshot());
            message_.clear();
            SDL_ShowSaveFileDialog(receive_selection, selection.release(), parent, &filter, 1, initial.c_str());
        }
        ImGui::EndDisabled();
        if (const auto *asset = document.opening_model())
        {
            ImGui::TextWrapped("Opening %s...", path_text(asset->filename()).c_str());
        }
        else if (!message_.empty())
        {
            ImGui::TextWrapped("%s", message_.c_str());
        }
        else if (!document.open_error().empty())
        {
            ImGui::TextWrapped("%s", document.open_error().c_str());
        }
    }

    void update(ViewerDocument &document)
    {
        if (!pending_ || pending_->selection.wait_for(std::chrono::seconds{0}) != std::future_status::ready)
        {
            return;
        }
        auto dialog = std::move(*pending_);
        pending_.reset();
        try
        {
            auto path = dialog.selection.get();
            if (path.empty())
            {
                return;
            }
            if (dialog.save_config)
            {
                document.session().check_output_path(path);
                save_scene_config(path, *dialog.save_config);
                message_ = "Saved " + path_text(path);
                document.saved_as(std::move(path));
            }
            else
            {
                document.open_model(path);
            }
        }
        catch (const std::exception &error)
        {
            message_ = std::format("{}: {}", dialog.save_config ? "Could not save scene" : "Could not open model",
                                   error.what());
        }
    }

  private:
    struct PendingDialog
    {
        std::future<Path> selection;
        std::optional<SceneConfig> save_config;
    };
    std::optional<PendingDialog> pending_;
    std::string message_;
};
} // namespace formlight
