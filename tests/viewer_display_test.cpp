#include <SDL3/SDL.h>
#include <imgui.h>

import std;
import formlight.scene_toml;
import formlight.viewer.document;
import formlight.viewer.session;
import formlight.viewer.window;

namespace
{
using namespace formlight;
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

SDL_Texture *displayed_texture(ImageExtent extent)
{
    // The SDL ImGui backend puts the submitted SDL_Texture pointer in each image draw command.
    for (const auto *list : ImGui::GetDrawData()->CmdLists)
    {
        for (const auto &cmd : list->CmdBuffer)
        {
            if (cmd.UserCallback || cmd.ElemCount == 0)
            {
                continue;
            }
            auto *texture = reinterpret_cast<SDL_Texture *>(cmd.GetTexID());
            if (texture && texture->w == extent.width && texture->h == extent.height &&
                SDL_GetNumberProperty(SDL_GetTextureProperties(texture), SDL_PROP_TEXTURE_ACCESS_NUMBER, -1) ==
                    SDL_TEXTUREACCESS_STREAMING)
            {
                return texture;
            }
        }
    }
    throw std::runtime_error("The viewer did not draw a scene texture of the expected size");
}

Surface read_texture(SDL_Texture *texture)
{
    // Streaming locks are write-only. Blit 1:1 into a separate target to exclude viewport scaling and overlays.
    auto *renderer = SDL_GetRendererFromTexture(texture);
    const std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> target{
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, texture->w, texture->h),
        SDL_DestroyTexture};
    if (!target || !SDL_SetRenderTarget(renderer, target.get()) ||
        !SDL_RenderTexture(renderer, texture, nullptr, nullptr))
    {
        throw std::runtime_error(std::format("Cannot copy the scene texture: {}", SDL_GetError()));
    }
    Surface pixels{SDL_RenderReadPixels(renderer, nullptr), SDL_DestroySurface};
    if (!pixels || !SDL_SetRenderTarget(renderer, nullptr))
    {
        throw std::runtime_error(std::format("Cannot read the scene texture: {}", SDL_GetError()));
    }
    return pixels;
}

std::array<Uint8, 4> rgba_at(SDL_Surface *surface, int x, int y)
{
    std::array<Uint8, 4> pixel{};
    if (!SDL_ReadSurfacePixel(surface, x, y, &pixel[0], &pixel[1], &pixel[2], &pixel[3]))
    {
        throw std::runtime_error(SDL_GetError());
    }
    return pixel;
}

std::uint64_t compare_png(const ViewerSession &session, std::string_view name, std::uint32_t samples)
{
    if (session.path_progress().value().completed != samples)
    {
        throw std::runtime_error(std::format("Expected the {}-sample frame for {}", samples, name));
    }
    const auto file = std::format("{}-{}.png", name, samples);
    const Surface reference{SDL_LoadPNG(file.c_str()), SDL_DestroySurface};
    if (!reference)
    {
        throw std::runtime_error(std::format("Cannot decode {}: {}", file, SDL_GetError()));
    }
    const auto actual = read_texture(displayed_texture({reference->w, reference->h}));
    if (actual->w != reference->w || actual->h != reference->h)
    {
        throw std::runtime_error("Readback extent differs from the PNG");
    }
    std::uint64_t rgb_sum{};
    for (int y = 0; y < actual->h; ++y)
    {
        for (int x = 0; x < actual->w; ++x)
        {
            const auto pixel = rgba_at(actual.get(), x, y);
            const auto expected = rgba_at(reference.get(), x, y);
            if (pixel != expected)
            {
                throw std::runtime_error(std::format("{} at ({}, {}): viewer {}, CLI {}", file, x, y, pixel, expected));
            }
            rgb_sum += pixel[0] + pixel[1] + pixel[2];
        }
    }
    std::println("{}: {} pixels match, RGB byte sum {}", file, actual->w * actual->h, rgb_sum);
    return rgb_sum;
}
} // namespace

int main()
try
{
    const auto config = load_scene_config("clip.toml");
    ViewerDocument document{"clip.toml", config};
    auto &session = document.session();
    ViewerWindow window{std::move(document)};
    const auto draw = [&]
    {
        if (!window.poll())
        {
            throw std::runtime_error("Window closed during display comparison");
        }
        window.draw();
    };
    const auto await_samples = [&](std::uint32_t count)
    {
        while (session.path_progress().value_or(PathProgress{}).completed < count)
        {
            draw();
        }
    };

    await_samples(8);
    compare_png(session, "clip", 8);
    session.set_display_settings(load_scene_config("darker.toml").display);
    await_samples(16);
    compare_png(session, "darker", 16);
    await_samples(19);
    const auto darker = compare_png(session, "darker", 19);

    session.set_display_settings(config.display);
    draw();
    const auto clipped = compare_png(session, "clip", 19);
    session.set_display_settings(load_scene_config("reinhard.toml").display);
    draw();
    const auto mapped = compare_png(session, "reinhard", 19);
    if (darker >= clipped || mapped >= clipped)
    {
        throw std::runtime_error("Lower exposure and Reinhard should each darken the settled image");
    }
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
