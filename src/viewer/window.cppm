module;

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

export module formlight.viewer.window;

import std;
import formlight.preview;

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

std::optional<PixelCoord> draw_image(SDL_Texture *texture, std::optional<PixelCoord> selection)
{
    const auto space = ImGui::GetContentRegionAvail();
    const auto origin = ImGui::GetCursorScreenPos();
    if (space.x < 1 || space.y < 1)
    {
        return {};
    }
    const float scale = std::min(space.x / static_cast<float>(texture->w), space.y / static_cast<float>(texture->h));
    const ImVec2 size{static_cast<float>(texture->w) * scale, static_cast<float>(texture->h) * scale};
    ImGui::SetCursorScreenPos({origin.x + (space.x - size.x) / 2, origin.y + (space.y - size.y) / 2});
    ImGui::InvisibleButton("##image", size);
    const auto lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest);
    draw->AddImage(reinterpret_cast<ImTextureID>(texture), lo, hi);
    draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear);
    if (selection)
    {
        const auto center = pixel_center(selection->x, selection->y);
        const ImVec2 mark{lo.x + static_cast<float>(center.x / texture->w) * (hi.x - lo.x),
                          lo.y + static_cast<float>(center.y / texture->h) * (hi.y - lo.y)};
        draw->AddCircle(mark, 5.0f, IM_COL32_BLACK, 0, 3.0f);
        draw->AddCircle(mark, 5.0f, IM_COL32_WHITE);
    }
    if (!ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        return {};
    }
    const auto mouse = ImGui::GetMousePos();
    // ImGui floors mouse positions to logical pixels. Pick at the center of that pixel.
    const auto texel = [](float position, float start, float end, int extent)
    {
        const double offset = (static_cast<double>(position) + 0.5 - start) / (end - start);
        return static_cast<int>(std::clamp(offset * extent, 0.0, static_cast<double>(extent - 1)));
    };
    return PixelCoord{texel(mouse.x, lo.x, hi.x, texture->w), texel(mouse.y, lo.y, hi.y, texture->h)};
}

export class ViewerWindow
{
  public:
    explicit ViewerWindow(const char *title);
    ViewerWindow(const ViewerWindow &) = delete;
    ViewerWindow &operator=(const ViewerWindow &) = delete;
    [[nodiscard]] bool poll();
    void upload(const ColorImage &image);
    [[nodiscard]] std::optional<PixelCoord> draw(const std::optional<PixelDiagnostic> &report,
                                                 std::optional<PixelCoord> selection);

  private:
    SdlVideo video_;
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window_{nullptr, SDL_DestroyWindow};
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer_{nullptr, SDL_DestroyRenderer};
    std::unique_ptr<ImGuiContext, GuiShutdown> context_;
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture_{nullptr, SDL_DestroyTexture};
    bool vsync_{};
};

ViewerWindow::ViewerWindow(const char *title)
{
    window_.reset(SDL_CreateWindow(title, 1040, 640, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY));
    require_sdl(window_ != nullptr, "SDL_CreateWindow");
    renderer_.reset(SDL_CreateRenderer(window_.get(), nullptr));
    require_sdl(renderer_ != nullptr, "SDL_CreateRenderer");
    vsync_ = SDL_SetRenderVSync(renderer_.get(), 1);
    IMGUI_CHECKVERSION();
    context_.reset(ImGui::CreateContext());
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().Fonts->AddFontDefaultVector();
    ImGui::StyleColorsDark();
    ImGui::GetStyle().FontSizeBase = 16.0f;
    require_sdl(ImGui_ImplSDL3_InitForSDLRenderer(window_.get(), renderer_.get()), "ImGui SDL3 platform");
    require_sdl(ImGui_ImplSDLRenderer3_Init(renderer_.get()), "ImGui SDL3 renderer");
}

bool ViewerWindow::poll()
{
    SDL_Event event{};
    while (SDL_PollEvent(&event))
    {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window_.get())))
        {
            return false;
        }
    }
    return true;
}

void ViewerWindow::upload(const ColorImage &image)
{
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
            out = std::ranges::copy(display_rgb8(image[y, x]), out).out;
            *out++ = 255;
        }
    }
    SDL_UnlockTexture(texture_.get());
}

std::optional<PixelCoord> ViewerWindow::draw(const std::optional<PixelDiagnostic> &report,
                                             std::optional<PixelCoord> selection)
{
    if ((SDL_GetWindowFlags(window_.get()) & SDL_WINDOW_MINIMIZED) != 0)
    {
        SDL_Delay(16);
        return {};
    }
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    const auto *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::Begin("##viewer", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoScrollWithMouse);
    const float width = ImGui::GetContentRegionAvail().x;
    const float panel = std::min(width * 0.4f, 16 * ImGui::GetFontSize());
    std::optional<PixelCoord> clicked;
    if (ImGui::BeginChild("Viewport", {std::max(1.0f, width - panel - ImGui::GetStyle().ItemSpacing.x), 0}, 0,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        if (texture_)
        {
            clicked = draw_image(texture_.get(), selection);
        }
        else
        {
            ImGui::TextUnformatted("Rendering scene...");
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("Inspector"))
    {
        ImGui::SeparatorText("Pixel inspector");
        if ((selection && !report) || clicked)
        {
            ImGui::TextUnformatted("Inspecting pixel...");
        }
        else if (report)
        {
            show_report(*report);
        }
        else if (texture_)
        {
            ImGui::TextWrapped("Click the image to inspect a pixel.");
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
    return clicked;
}
} // namespace formlight
