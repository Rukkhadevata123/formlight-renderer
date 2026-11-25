import std;
import formlight.cli;
import formlight.preview;
import formlight.image_codec;

auto main(int argc, char **argv) -> int
{
    if (argc == 1 || (argc == 2 && std::string_view{argv[1]} == "--help"))
    {
        std::println("Usage: formlight-renderer --inspect MODEL.glb\n"
                     "       formlight-renderer --render MODEL.glb --output IMAGE.png [--ground]\n"
                     "                         [--shading base-color|diffuse] [--shadows none|map|ray]\n"
                     "                         [--toward-light X Y Z | --area-light X Y Z WIDTH HEIGHT]\n"
                     "                         [--light-samples N] [--seed N] [--light-power POWER]\n"
                     "The rectangle faces -Y. Its default power is 240, with 16 samples per pixel and seed 0.");
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
