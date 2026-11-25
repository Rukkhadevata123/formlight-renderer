export module formlight.cli;

import std;
import formlight.preview;

namespace formlight
{
export struct RenderOptions
{
    std::filesystem::path output;
    std::optional<PreviewLighting> lighting;
    bool ground{};
};

template <class Number> Number parse_number(std::string_view option, std::string_view value)
{
    Number component{};
    const auto end = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(value.data(), end, component);
    if (error != std::errc{} || ptr != end || !std::isfinite(component))
    {
        throw std::invalid_argument(std::format("Invalid value for {}: {}", option, value));
    }
    return component;
}

export RenderOptions parse_render_options(std::span<char *> arguments)
{
    RenderOptions options;
    std::optional<bool> diffuse;
    struct Rectangle
    {
        Vec3 center;
        double width, height;
    };
    std::optional<DirectionalLight> directional;
    std::optional<Rectangle> rectangle;
    std::optional<ShadowMethod> shadows;
    LightSampling sampling;
    double power = 240;
    bool has_area_parameters = false;

    const auto take_value = [&](std::string_view option) -> std::string_view
    {
        if (arguments.empty())
        {
            throw std::invalid_argument(std::format("Missing value for {}", option));
        }
        const std::string_view value = arguments.front();
        arguments = arguments.subspan(1);
        return value;
    };
    const auto real = [&](std::string_view option) { return parse_number<double>(option, take_value(option)); };
    const auto vector = [&](std::string_view option) -> Vec3 { return {real(option), real(option), real(option)}; };
    while (!arguments.empty())
    {
        const auto option = take_value("--render");
        if (option == "--ground")
        {
            options.ground = true;
        }
        else if (option == "--toward-light")
        {
            directional.emplace(vector(option));
        }
        else if (option == "--area-light")
        {
            rectangle = Rectangle{vector(option), real(option), real(option)};
        }
        else if (option == "--light-samples")
        {
            sampling.samples = parse_number<std::uint32_t>(option, take_value(option));
            if (sampling.samples == 0)
            {
                throw std::invalid_argument("--light-samples must be positive");
            }
            has_area_parameters = true;
        }
        else if (option == "--seed")
        {
            sampling.seed = parse_number<std::uint64_t>(option, take_value(option));
            has_area_parameters = true;
        }
        else if (option == "--light-power")
        {
            power = real(option);
            has_area_parameters = true;
        }
        else if (option == "--output" && options.output.empty())
        {
            options.output = take_value(option);
        }
        else if (option == "--shading")
        {
            const auto value = take_value(option);
            if (value != "base-color" && value != "diffuse")
            {
                throw std::invalid_argument("Expected --shading base-color or diffuse");
            }
            diffuse = value == "diffuse";
        }
        else if (option == "--shadows")
        {
            const auto value = take_value(option);
            if (value == "none")
            {
                shadows = ShadowMethod::None;
            }
            else if (value == "map")
            {
                shadows = ShadowMethod::Map;
            }
            else if (value == "ray")
            {
                shadows = ShadowMethod::Ray;
            }
            else
            {
                throw std::invalid_argument("Expected --shadows none, map or ray");
            }
        }
        else
        {
            throw std::invalid_argument(std::format("Unexpected render option: {}", option));
        }
    }
    if (options.output.empty())
    {
        throw std::invalid_argument("Expected --output IMAGE.png");
    }
    if (rectangle)
    {
        if (directional)
        {
            throw std::invalid_argument("Choose one light: --area-light or --toward-light");
        }
        if (diffuse == false || shadows == ShadowMethod::Map)
        {
            throw std::invalid_argument("--area-light requires diffuse shading and supports --shadows ray or none");
        }
        if (rectangle->width <= 0 || rectangle->height <= 0)
        {
            throw std::invalid_argument("Rectangle light width and height must be positive");
        }
        const RectangleLight light{rectangle->center, {rectangle->width, 0, 0}, {0, 0, rectangle->height}, power};
        options.lighting = AreaLighting{light, sampling, shadows.value_or(ShadowMethod::Ray) == ShadowMethod::Ray};
        return options;
    }
    if (has_area_parameters)
    {
        throw std::invalid_argument("--light-samples, --light-power and --seed require --area-light");
    }
    DirectionalLighting lighting;
    lighting.shadows = shadows.value_or(ShadowMethod::None);
    const bool needs_light = directional.has_value() || lighting.shadows != ShadowMethod::None;
    if (diffuse.value_or(needs_light))
    {
        if (directional)
        {
            lighting.light = *directional;
        }
        options.lighting = lighting;
    }
    else if (needs_light)
    {
        throw std::invalid_argument("Directional lighting and shadows require diffuse shading");
    }
    return options;
}
} // namespace formlight
