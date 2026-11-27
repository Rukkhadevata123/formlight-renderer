export module formlight.cli;

import std;
import formlight.scene_config;
import formlight.preview;

namespace formlight
{
template <class T> T integer_option(std::string_view option, std::string_view value)
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

export using RenderAction = std::variant<std::filesystem::path, PixelCoord>;

// Only explicitly supplied overrides modify the loaded scene.
export RenderAction apply_render_options(SceneConfig &config, std::span<char *> args)
{
    std::optional<RenderAction> action;
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
            action = PixelCoord{integer_option<int>(option, value), integer_option<int>(option, args.front())};
            args = args.subspan(1);
        }
        else if (option == "--output")
        {
            if (value.empty())
            {
                throw std::invalid_argument("Expected --output IMAGE.png");
            }
            action = std::filesystem::path{value};
        }
        else if (option == "--mode")
        {
            config.render.mode = parse_choice<RenderMode>(value);
        }
        else if (option == "--ssaa")
        {
            config.render.ssaa = integer_option<int>(option, value);
        }
        else if (option == "--light-samples")
        {
            config.render.sampling.samples = integer_option<std::uint32_t>(option, value);
        }
        else if (option == "--seed")
        {
            config.render.sampling.seed = integer_option<std::uint64_t>(option, value);
        }
        else if (option == "--path-samples")
        {
            config.render.path_samples = integer_option<std::uint32_t>(option, value);
        }
        else if (option == "--max-bounces")
        {
            config.render.max_bounces = integer_option<std::uint32_t>(option, value);
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
    return *action;
}

export void print_pixel_report(const PixelDiagnostic &report);
} // namespace formlight
