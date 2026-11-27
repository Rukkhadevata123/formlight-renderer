import std;
import formlight.cli;
import formlight.preview;
import formlight.image_codec;
import formlight.scene_toml;

auto main(int argc, char **argv) -> int
{
    if (argc == 1 || (argc == 2 && std::string_view{argv[1]} == "--help"))
    {
        std::println("Usage: formlight-renderer --inspect MODEL\n"
                     "       formlight-renderer --render MODEL (--output IMAGE.png | --pixel X Y)\n"
                     "       formlight-renderer --scene SCENE.toml (--output IMAGE.png | --pixel X Y)\n"
                     "                         [--mode raster|hybrid] [--light-samples N] [--seed N]\n"
                     "MODEL can be a .gltf or .glb file. Pixel coordinates start at (0, 0) in the top-left corner.");
        return 0;
    }

    if (argc == 3 && std::string_view{argv[1]} == "--inspect")
    {
        const auto scene = formlight::GltfScene::load(argv[2], formlight::GltfContent::Geometry);
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

    if (argc >= 3 && (std::string_view{argv[1]} == "--render" || std::string_view{argv[1]} == "--scene"))
    {
        using namespace formlight;
        try
        {
            const std::filesystem::path input = argv[2];
            SceneConfig config;
            if (std::string_view{argv[1]} == "--scene")
            {
                config = load_scene_config(input);
            }
            else
            {
                config.scene.asset = std::filesystem::absolute(input);
            }
            const auto action = apply_render_options(config, {argv + 3, static_cast<std::size_t>(argc - 3)});
            const auto request = prepare_render(config);
            if (const auto *pixel = std::get_if<PixelCoord>(&action))
            {
                print_pixel_report(diagnose_pixel(request, *pixel));
                return 0;
            }
            const auto &output = std::get<std::filesystem::path>(action);
            if (std::filesystem::exists(output) && std::filesystem::equivalent(input, output))
            {
                throw std::invalid_argument("Output image would overwrite the input model or scene");
            }
            const auto preview = load_preview(request);
            preview.scene.check_output_path(output);
            const auto image = render_preview(preview.scene, preview.view, request.lighting);
            write_png(output, image, config.display);
            std::println("Wrote {} ({} x {})", output.string(), request.extent.width, request.extent.height);
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
