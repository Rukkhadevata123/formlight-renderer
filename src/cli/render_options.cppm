export module formlight.cli;

import std;
import formlight.scene_config;
import formlight.preview;

namespace formlight
{
template <class T> T numeric_option(std::string_view option, std::string_view value)
{
    T result{};
    const auto end = value.data() + value.size();
    const auto [last, error] = std::from_chars(value.data(), end, result);
    if (error != std::errc{} || last != end)
    {
        throw std::invalid_argument(std::format("Invalid value for {}: {}", option, value));
    }
    return result;
}

export struct ImageOutput
{
    std::filesystem::path path;
    std::uint32_t batch_size{std::numeric_limits<std::uint32_t>::max()};
};
export using RenderAction = std::variant<ImageOutput, PixelCoord>;

// Only explicitly supplied overrides modify the loaded scene.
export RenderAction apply_render_options(SceneConfig &config, std::span<char *> args)
{
    std::optional<RenderAction> action;
    std::optional<std::uint32_t> batch_size;
    while (!args.empty())
    {
        const std::string_view option = args.front();
        if (args.size() < 2 || std::string_view{args[1]}.starts_with("--"))
        {
            throw std::invalid_argument(std::format("Missing value for {}", option));
        }
        const std::string_view value = args[1];
        args = args.subspan(2);
        if (action && (option == "--output" || option == "--pixel"))
        {
            throw std::invalid_argument("Choose one output action: --output IMAGE.png or --pixel X Y");
        }
        if (option == "--pixel")
        {
            if (args.empty() || std::string_view{args.front()}.starts_with("--"))
            {
                throw std::invalid_argument("Expected --pixel X Y");
            }
            action = PixelCoord{numeric_option<int>(option, value), numeric_option<int>(option, args.front())};
            args = args.subspan(1);
        }
        else if (option == "--output")
        {
            if (value.empty())
            {
                throw std::invalid_argument("Expected --output IMAGE.png");
            }
            action = ImageOutput{std::filesystem::path{value}};
        }
        else if (option == "--mode")
        {
            config.render.mode = parse_choice<RenderMode>(value);
        }
        else if (option == "--defocus-angle")
        {
            config.camera.lens.defocus_angle_degrees = numeric_option<double>(option, value);
        }
        else if (option == "--focus-distance")
        {
            config.camera.lens.focus_distance = numeric_option<double>(option, value);
        }
        else if (option == "--ssaa")
        {
            config.render.ssaa = numeric_option<int>(option, value);
        }
        else if (option == "--light-samples")
        {
            config.render.sampling.samples = numeric_option<std::uint32_t>(option, value);
        }
        else if (option == "--seed")
        {
            config.render.sampling.seed = numeric_option<std::uint64_t>(option, value);
        }
        else if (option == "--scene-seed")
        {
            if (config.scene.preset == ScenePreset::None)
            {
                throw std::invalid_argument("--scene-seed requires a generated scene");
            }
            config.scene.seed = numeric_option<std::uint64_t>(option, value);
        }
        else if (option == "--path-samples")
        {
            config.render.path_samples = numeric_option<std::uint32_t>(option, value);
        }
        else if (option == "--batch-size")
        {
            batch_size = numeric_option<std::uint32_t>(option, value);
        }
        else if (option == "--max-bounces")
        {
            config.render.max_bounces = numeric_option<std::uint32_t>(option, value);
        }
        else if (option == "--bounce-sampling")
        {
            config.render.bounce_sampling = parse_choice<BounceSampling>(value);
        }
        else
        {
            throw std::invalid_argument(std::format("Unexpected render option: {}", option));
        }
    }
    if (!action)
    {
        throw std::invalid_argument("Expected --output IMAGE.png or --pixel X Y");
    }
    if (batch_size)
    {
        auto *image = std::get_if<ImageOutput>(&*action);
        if (!image || config.render.mode != RenderMode::Path || *batch_size == 0)
        {
            throw std::invalid_argument("--batch-size requires a positive count, path mode and --output");
        }
        image->batch_size = *batch_size;
    }
    return std::move(*action);
}

export void print_pixel_report(const PixelDiagnostic &report);
} // namespace formlight
