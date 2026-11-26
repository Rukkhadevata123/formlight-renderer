module;

#include <SDL3/SDL.h>
#include <imgui.h>

export module formlight.viewer.appearance;

import std;

namespace formlight
{
export class ViewerAppearance
{
  public:
    void initialize()
    {
        auto &io = ImGui::GetIO();
        constexpr auto font_file = "fonts/NotoSansSC-Regular.otf";
        if (const char *base = SDL_GetBasePath())
        {
            const auto path = std::format("{}{}", base, font_file);
            ImFontConfig config;
            config.Flags |= ImFontFlags_NoLoadError;
            io.FontDefault = io.Fonts->AddFontFromFileTTF(path.c_str(), 0.0f, &config);
        }
        if (!io.FontDefault)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not load %s; using the built-in font.", font_file);
            io.FontDefault = io.Fonts->AddFontDefaultVector();
        }
        ImGui::StyleColorsDark();
        auto &style = ImGui::GetStyle();
        style.FontSizeBase = 17.0f;
        unscaled_ = style;
    }

    void update_scale(float display_scale, float pixel_density) const
    {
        if (!(display_scale > 0 && pixel_density > 0))
        {
            return;
        }
        // The backend already bakes glyphs at framebuffer density. Layout uses window coordinates.
        const float scale = display_scale / pixel_density;
        auto &style = ImGui::GetStyle();
        if (style.FontScaleDpi != scale)
        {
            style = unscaled_;
            style.ScaleAllSizes(scale);
            style.FontScaleDpi = scale;
        }
    }

  private:
    ImGuiStyle unscaled_;
};
} // namespace formlight
