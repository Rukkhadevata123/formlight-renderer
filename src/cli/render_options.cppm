export module formlight.cli;

import std;
import formlight.scene_config;

namespace formlight
{
template <class T> T unsigned_option(std::string_view option, std::string_view value)
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

// Only explicitly supplied overrides modify the loaded scene.
export std::filesystem::path apply_render_options(SceneConfig &config, std::span<char *> args)
{
    std::filesystem::path output;
    while (!args.empty())
    {
        const std::string_view option = args.front();
        if (args.size() < 2 || std::string_view{args[1]}.starts_with("--"))
        {
            throw std::invalid_argument(std::format("Missing value for {}", option));
        }
        const std::string_view value = args[1];
        args = args.subspan(2);
        if (option == "--output" && output.empty())
        {
            output = value;
        }
        else if (option == "--mode")
        {
            config.render.mode = parse_choice<RenderMode>(value);
        }
        else if (option == "--light-samples")
        {
            config.render.sampling.samples = unsigned_option<std::uint32_t>(option, value);
        }
        else if (option == "--seed")
        {
            config.render.sampling.seed = unsigned_option<std::uint64_t>(option, value);
        }
        else
        {
            throw std::invalid_argument(std::format("Unexpected render option: {}", option));
        }
    }
    if (output.empty())
    {
        throw std::invalid_argument("Expected --output IMAGE.png");
    }
    return output;
}
} // namespace formlight
