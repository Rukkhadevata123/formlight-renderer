export module formlight.viewer.document;

import std;
import formlight.preview;
import formlight.viewer.session;

namespace formlight
{
// Each session owns its assets and all workers borrowing them. Publish a replacement with its first full image.
export class ViewerDocument
{
  public:
    ViewerDocument(const std::filesystem::path &source, SceneConfig config)
        : scene_file_(std::filesystem::absolute(source)), active_(std::make_unique<ViewerSession>(std::move(config)))
    {
    }

    [[nodiscard]] ViewerSession &session()
    {
        return *active_;
    }
    [[nodiscard]] const std::filesystem::path &display_path() const
    {
        return scene_file_ ? *scene_file_ : active_->applied_config().scene.asset;
    }
    [[nodiscard]] std::filesystem::path save_location() const
    {
        if (scene_file_)
        {
            return *scene_file_;
        }
        auto path = display_path();
        path.replace_extension(".toml");
        return path;
    }
    void saved_as(std::filesystem::path path)
    {
        scene_file_ = std::move(path);
        open_error_.clear();
    }
    [[nodiscard]] const std::filesystem::path *opening_model() const
    {
        return opening_ ? &opening_->applied_config().scene.asset : nullptr;
    }
    [[nodiscard]] const std::string &open_error() const
    {
        return open_error_;
    }

    void open_model(const std::filesystem::path &path)
    {
        if (opening_)
        {
            throw std::logic_error("A model is already opening");
        }
        auto config = active_->applied_config();
        config.scene.asset = std::filesystem::absolute(path);
        config.camera.auto_frame = true;
        opening_ = std::make_unique<ViewerSession>(std::move(config));
        open_error_.clear();
    }

    void apply_settings()
    {
        active_->apply_settings();
        if (opening_)
        {
            opening_->render_settings() = active_->applied_config().render;
            opening_->light_settings() = active_->applied_config().light;
            opening_->apply_settings();
        }
    }

    [[nodiscard]] const ColorImage *update(const ViewportInput &input)
    {
        const auto *image = active_->update(input);
        if (!opening_)
        {
            return image;
        }
        try
        {
            if (const auto *first = opening_->update({}))
            {
                // Keep unapplied edits in the inspector. The image was rendered with applied values.
                opening_->render_settings() = active_->render_settings();
                opening_->light_settings() = active_->light_settings();
                active_ = std::move(opening_);
                scene_file_.reset();
                return first;
            }
        }
        catch (const std::exception &error)
        {
            const auto bytes = opening_model()->u8string();
            const std::string path{bytes.begin(), bytes.end()};
            const std::string_view reason{error.what()};
            open_error_ = reason.starts_with(path) ? std::string(reason) : std::format("{}: {}", path, reason);
            opening_.reset();
        }
        return image;
    }

  private:
    std::optional<std::filesystem::path> scene_file_;
    std::unique_ptr<ViewerSession> active_, opening_;
    std::string open_error_;
};
} // namespace formlight
