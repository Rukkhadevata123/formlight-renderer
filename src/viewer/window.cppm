module;

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

export module formlight.viewer.window;

import std;
import formlight.preview;
import formlight.viewer.appearance;
import formlight.viewer.document;
import formlight.viewer.file_dialogs;
import formlight.viewer.session;
import formlight.viewer.settings_panel;

namespace formlight
{
void require_sdl(bool success, const char *operation)
{
    if (!success)
    {
        throw std::runtime_error(std::format("{}: {}", operation, SDL_GetError()));
    }
}

struct SdlVideo
{
    SdlVideo()
    {
        require_sdl(SDL_Init(SDL_INIT_VIDEO), "SDL_Init");
    }
    ~SdlVideo()
    {
        SDL_Quit();
    }
};

struct GuiShutdown
{
    void operator()(ImGuiContext *context) const noexcept
    {
        ImGui::SetCurrentContext(context);
        if (ImGui::GetIO().BackendRendererUserData)
        {
            ImGui_ImplSDLRenderer3_Shutdown();
        }
        if (ImGui::GetIO().BackendPlatformUserData)
        {
            ImGui_ImplSDL3_Shutdown();
        }
        ImGui::DestroyContext(context);
    }
};

void show_report(const PixelDiagnostic &report)
{
    ImGui::Text("Pixel (%d, %d)", report.pixel.x, report.pixel.y);
    if (const auto &surface = report.surface)
    {
        const auto id = surface->hit.surface.id;
        ImGui::Text("Instance %u", std::to_underlying(id.instance));
        ImGui::Text("Primitive %u", std::to_underlying(id.primitive));
        ImGui::Text("Material slot %zu", surface->material_index);
    }
    else
    {
        ImGui::TextUnformatted("Background");
    }
    if (!report.samples.empty())
    {
        const auto blocked = std::ranges::count_if(report.samples, [](const LightSample &sample)
                                                   { return sample.shadow.state == ShadowState::Blocked; });
        ImGui::Text("Blocked samples   %zu / %zu", static_cast<std::size_t>(blocked), report.samples.size());
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Linear RGB");
    ImGui::Text("R   %.7g\nG   %.7g\nB   %.7g", report.color.r, report.color.g, report.color.b);
}

struct ImageRect
{
    ImVec2 lo, hi;
    [[nodiscard]] PixelCoord pixel_at(ImVec2 mouse, ImageExtent extent) const
    {
        // ImGui floors mouse positions to logical pixels. Pick at the center of that pixel.
        const auto texel = [](float position, float start, float end, int count)
        {
            const double offset = (static_cast<double>(position) + 0.5 - start) / (end - start);
            return static_cast<int>(std::clamp(offset * count, 0.0, static_cast<double>(count - 1)));
        };
        return {texel(mouse.x, lo.x, hi.x, extent.width), texel(mouse.y, lo.y, hi.y, extent.height)};
    }
};

ImageRect fit_image(ImVec2 origin, ImVec2 space, ImageExtent extent)
{
    const float scale =
        std::min(space.x / static_cast<float>(extent.width), space.y / static_cast<float>(extent.height));
    const ImVec2 size{static_cast<float>(extent.width) * scale, static_cast<float>(extent.height) * scale};
    const ImVec2 lo{origin.x + (space.x - size.x) / 2, origin.y + (space.y - size.y) / 2};
    return {lo, {lo.x + size.x, lo.y + size.y}};
}

void draw_image(SDL_Texture *texture, ImageRect rect, std::optional<PixelCoord> selection)
{
    const auto [lo, hi] = rect;
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest);
    draw->AddImage(reinterpret_cast<ImTextureID>(texture), lo, hi);
    draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear);
    if (selection)
    {
        const auto center = pixel_center(selection->x, selection->y);
        const ImVec2 mark{lo.x + static_cast<float>(center.x / texture->w) * (hi.x - lo.x),
                          lo.y + static_cast<float>(center.y / texture->h) * (hi.y - lo.y)};
        const float scale = ImGui::GetStyle().FontScaleDpi;
        draw->AddCircle(mark, 5.0f * scale, IM_COL32_BLACK, 0, 3.0f * scale);
        draw->AddCircle(mark, 5.0f * scale, IM_COL32_WHITE, 0, scale);
    }
}

export class ViewerWindow
{
  public:
    explicit ViewerWindow(ViewerDocument &document);
    ViewerWindow(const ViewerWindow &) = delete;
    ViewerWindow &operator=(const ViewerWindow &) = delete;
    [[nodiscard]] bool poll();
    void draw();

  private:
    enum class Drag
    {
        None,
        Orbit,
        Pan
    };
    Drag drag_{};
    [[nodiscard]] ViewportInput read_input(ImageRect rect, ImageExtent extent);
    void upload(const ColorImage &image, const DisplaySettings &display);
    SdlVideo video_;
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window_{nullptr, SDL_DestroyWindow};
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer_{nullptr, SDL_DestroyRenderer};
    std::unique_ptr<ImGuiContext, GuiShutdown> context_;
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture_{nullptr, SDL_DestroyTexture};
    bool vsync_{};
    std::int64_t max_texture_size_{};
    ViewerAppearance appearance_;
    SettingsPanel settings_;
    ViewerDocument &document_;
    SceneFileDialogs files_;
    std::filesystem::path title_path_;
};

ViewerWindow::ViewerWindow(ViewerDocument &document) : document_(document), title_path_(document.display_path())
{
    const auto display = SDL_GetPrimaryDisplay();
    const float content_scale = SDL_GetDisplayContentScale(display);
    const float scale = content_scale > 0 ? content_scale : 1.0f;
    int width = static_cast<int>(std::lround(1040 * scale));
    int height = static_cast<int>(std::lround(640 * scale));
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(display, &usable))
    {
        width = std::min(width, usable.w);
        height = std::min(height, usable.h);
    }
    window_.reset(SDL_CreateWindow(SceneFileDialogs::window_title(document).c_str(), width, height,
                                   SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY));
    require_sdl(window_ != nullptr, "SDL_CreateWindow");
    renderer_.reset(SDL_CreateRenderer(window_.get(), nullptr));
    require_sdl(renderer_ != nullptr, "SDL_CreateRenderer");
    max_texture_size_ =
        SDL_GetNumberProperty(SDL_GetRendererProperties(renderer_.get()), SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0);
    vsync_ = SDL_SetRenderVSync(renderer_.get(), 1);
    IMGUI_CHECKVERSION();
    context_.reset(ImGui::CreateContext());
    ImGui::GetIO().IniFilename = nullptr;
    appearance_.initialize();
    require_sdl(ImGui_ImplSDL3_InitForSDLRenderer(window_.get(), renderer_.get()), "ImGui SDL3 platform");
    require_sdl(ImGui_ImplSDLRenderer3_Init(renderer_.get()), "ImGui SDL3 renderer");
}

bool ViewerWindow::poll()
{
    bool open = true;
    SDL_Event event{};
    while (SDL_PollEvent(&event))
    {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window_.get())))
        {
            open = false;
        }
    }
    files_.update(document_);
    return open;
}

void ViewerWindow::upload(const ColorImage &image, const DisplaySettings &display)
{
    const DisplayTransform transform{display};
    const auto [width, height] = image.extent();
    if (!texture_ || texture_->w != width || texture_->h != height)
    {
        texture_.reset(
            SDL_CreateTexture(renderer_.get(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width, height));
        require_sdl(texture_ != nullptr, "SDL_CreateTexture");
        require_sdl(SDL_SetTextureBlendMode(texture_.get(), SDL_BLENDMODE_NONE), "SDL_SetTextureBlendMode");
    }
    void *pixels{};
    int pitch{};
    require_sdl(SDL_LockTexture(texture_.get(), nullptr, &pixels, &pitch), "SDL_LockTexture");
    for (int y = 0; y < height; ++y)
    {
        auto *out = static_cast<std::uint8_t *>(pixels) + static_cast<std::size_t>(y) * pitch;
        for (int x = 0; x < width; ++x)
        {
            out = std::ranges::copy(transform.rgb8(image[y, x]), out).out;
            *out++ = 255;
        }
    }
    SDL_UnlockTexture(texture_.get());
}

ViewportInput ViewerWindow::read_input(ImageRect rect, ImageExtent extent)
{
    const auto &io = ImGui::GetIO();
    ViewportInput input;
    if (ImGui::IsItemHovered())
    {
        input.wheel = io.MouseWheel;
        input.focus = !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            input.picked = rect.pixel_at(ImGui::GetMousePos(), extent);
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) || !ImGui::IsItemActive())
    {
        drag_ = Drag::None;
    }
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Middle))
    {
        drag_ = Drag::None;
        if (io.KeyMods == ImGuiMod_None)
        {
            drag_ = Drag::Orbit;
        }
        if (io.KeyMods == ImGuiMod_Shift)
        {
            drag_ = Drag::Pan;
        }
    }
    else if (drag_ == Drag::Orbit)
    {
        const float scale = ImGui::GetStyle().FontScaleDpi;
        input.orbit_pixels = {io.MouseDelta.x / scale, io.MouseDelta.y / scale};
    }
    else if (drag_ == Drag::Pan)
    {
        const double height = rect.hi.y - rect.lo.y;
        input.pan_heights = {io.MouseDelta.x / height, io.MouseDelta.y / height};
    }
    return input;
}

void ViewerWindow::draw()
{
    if ((SDL_GetWindowFlags(window_.get()) & SDL_WINDOW_MINIMIZED) != 0)
    {
        SDL_Delay(16);
        return;
    }
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    appearance_.update_scale(SDL_GetWindowDisplayScale(window_.get()), SDL_GetWindowPixelDensity(window_.get()));
    ImGui::NewFrame();
    const auto *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::Begin("##viewer", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoScrollWithMouse);
    const float width = ImGui::GetContentRegionAvail().x;
    const float panel = std::min(width * 0.4f, 20 * ImGui::GetFontSize());
    const bool visible =
        ImGui::BeginChild("Viewport", {std::max(1.0f, width - panel - ImGui::GetStyle().ItemSpacing.x), 0}, 0,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const auto origin = ImGui::GetCursorScreenPos(), space = ImGui::GetContentRegionAvail();
    const bool show_image = visible && space.x >= 1 && space.y >= 1;
    ViewportInput input;
    if (show_image && texture_)
    {
        const auto rect = fit_image(origin, space, document_.session().display_extent());
        ImGui::SetCursorScreenPos(rect.lo);
        ImGui::InvisibleButton("##image", {rect.hi.x - rect.lo.x, rect.hi.y - rect.lo.y},
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        input = read_input(rect, {texture_->w, texture_->h});
    }
    if (const auto *image = document_.update(input))
    {
        upload(*image, document_.session().applied_config().display);
    }
    if (title_path_ != document_.display_path())
    {
        title_path_ = document_.display_path();
        SDL_SetWindowTitle(window_.get(), SceneFileDialogs::window_title(document_).c_str());
    }
    const auto &session = document_.session();
    if (show_image)
    {
        // A newly published frame may have a different aspect ratio from the image just picked.
        const auto rect = fit_image(origin, space, session.display_extent());
        if (texture_)
        {
            draw_image(texture_.get(), rect, session.selection());
        }
        else
        {
            ImGui::GetWindowDrawList()->AddText(rect.lo, ImGui::GetColorU32(ImGuiCol_Text), "Rendering scene...");
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("Inspector"))
    {
        settings_.draw(document_, max_texture_size_);
        files_.draw(window_.get(), document_, texture_ != nullptr);
        ImGui::SeparatorText("Pixel inspector");
        if (session.selection() && !session.report())
        {
            ImGui::TextUnformatted("Inspecting pixel...");
        }
        else if (const auto &report = session.report())
        {
            show_report(*report);
        }
        else if (texture_)
        {
            ImGui::TextWrapped("Click the image to inspect a pixel.");
        }
        ImGui::SeparatorText("Navigation");
        ImGui::TextWrapped("Middle-drag to orbit\nShift + middle-drag to pan\nScroll to zoom\nF to frame selection");
        if (texture_ && session.showing_preview())
        {
            ImGui::TextDisabled("Navigation preview: %d x %d", texture_->w, texture_->h);
        }
        else if (texture_ && session.updating())
        {
            ImGui::TextDisabled("Updating view...");
        }
    }
    ImGui::EndChild();
    ImGui::End();
    ImGui::Render();
    const auto density = ImGui::GetIO().DisplayFramebufferScale;
    require_sdl(SDL_SetRenderScale(renderer_.get(), density.x, density.y), "SDL_SetRenderScale");
    require_sdl(SDL_RenderClear(renderer_.get()), "SDL_RenderClear");
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_.get());
    require_sdl(SDL_RenderPresent(renderer_.get()), "SDL_RenderPresent");
    if (!vsync_)
    {
        SDL_Delay(16);
    }
}
} // namespace formlight
