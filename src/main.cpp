import std;
import formlight.preview;
import formlight.image_codec;

namespace
{
struct RenderOptions
{
    std::filesystem::path output;
    std::optional<formlight::PreviewLighting> lighting;
    bool ground{};
};

double direction_component(std::string_view value)
{
    double component{};
    const auto end = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(value.data(), end, component);
    if (error != std::errc{} || ptr != end || !std::isfinite(component))
    {
        throw std::invalid_argument(std::format("Invalid light direction component: {}", value));
    }
    return component;
}

RenderOptions parse_render_options(std::span<char *> arguments)
{
    using namespace formlight;
    RenderOptions options;
    std::optional<bool> diffuse;
    std::optional<DirectionalLight> light;
    ShadowMethod shadows = ShadowMethod::None;
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
    while (!arguments.empty())
    {
        const auto option = take_value("--render");
        if (option == "--ground")
        {
            options.ground = true;
        }
        else if (option == "--toward-light")
        {
            const double x = direction_component(take_value(option));
            const double y = direction_component(take_value(option));
            const double z = direction_component(take_value(option));
            light.emplace(Vec3{x, y, z});
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
    const bool needs_light = light.has_value() || shadows != ShadowMethod::None;
    if (diffuse.value_or(needs_light))
    {
        auto &lighting = options.lighting.emplace();
        lighting.shadows = shadows;
        if (light)
        {
            lighting.light = *light;
        }
    }
    else if (needs_light)
    {
        throw std::invalid_argument("Directional lighting and shadows require diffuse shading");
    }
    return options;
}
} // namespace

auto main(int argc, char **argv) -> int
{
    if (argc == 1 || (argc == 2 && std::string_view{argv[1]} == "--help"))
    {
        std::println(
            "Usage: formlight-renderer --inspect MODEL.glb\n"
            "       formlight-renderer --render MODEL.glb --output IMAGE.png [--ground]\n"
            "                         [--shading base-color|diffuse] [--toward-light X Y Z] [--shadows none|map|ray]");
        return 0;
    }

    if (argc == 3 && std::string_view{argv[1]} == "--inspect")
    {
        const auto scene = formlight::GltfScene::load_glb(argv[2], formlight::GltfContent::Geometry);
        if (!scene)
        {
            std::cerr << scene.error() << '\n';
            return 1;
        }
        std::size_t triangles = 0;
        for (const auto &instance : scene->instances())
        {
            triangles += instance.triangles.size();
        }
        std::println("Meshes: {}\nInstances: {}\nInstanced triangles: {}", scene->mesh_count(),
                     scene->instances().size(), triangles);
        return 0;
    }

    if (argc >= 3 && std::string_view{argv[1]} == "--render")
    {
        using namespace formlight;
        try
        {
            const std::filesystem::path input = argv[2];
            const auto options = parse_render_options({argv + 3, static_cast<std::size_t>(argc - 3)});
            if (std::filesystem::exists(options.output) && std::filesystem::equivalent(input, options.output))
            {
                throw std::invalid_argument("Input model and output image refer to the same file");
            }
            constexpr ImageExtent extent{640, 480};
            auto scene = GltfScene::load_glb(input, GltfContent::BaseColor);
            if (!scene)
            {
                std::cerr << scene.error() << '\n';
                return 1;
            }
            std::optional<Bounds3> subject;
            if (options.ground)
            {
                subject = world_bounds(scene->instances());
                add_preview_ground(*scene, *subject);
            }
            const auto image = render_preview(*scene, frame_scene(*scene, extent, subject), options.lighting);
            write_png(options.output, image);
            std::println("Wrote {} ({} x {})", options.output.string(), extent.width, extent.height);
            return 0;
        }
        catch (const std::exception &error)
        {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }

    std::cerr << "Unexpected arguments. Run formlight-renderer --help for usage.\n";
    return 1;
}
