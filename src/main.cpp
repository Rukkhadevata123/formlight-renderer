import std;
import formlight.cli;
import formlight.preview;
import formlight.image_codec;
import formlight.scene_toml;

auto main(int argc, char **argv) -> int
{
    if (argc == 1 || (argc == 2 && std::string_view{argv[1]} == "--help"))
    {
        std::println(
            "Usage: formlight-renderer --inspect MODEL\n"
            "       formlight-renderer --render MODEL (--output IMAGE.png | --pixel X Y)\n"
            "       formlight-renderer --scene SCENE.toml (--output IMAGE.png | --pixel X Y)\n"
            "       formlight-renderer --preset many_balls|final_scene (--output IMAGE.png | --pixel X Y) "
            "[--scene-seed N]\n"
            "                         [--mode raster|hybrid|path] [--ssaa 1|2] [--light-samples N] [--seed N]\n"
            "                         [--path-samples N] [--batch-size N] [--max-bounces N] [--bounce-sampling "
            "cosine|light_mixture]\n"
            "                         [--defocus-angle DEGREES] [--focus-distance DISTANCE]\n"
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

    const std::string_view command = argv[1];
    if (argc >= 3 && (command == "--render" || command == "--scene" || command == "--preset"))
    {
        using namespace formlight;
        try
        {
            const std::filesystem::path input = argv[2];
            SceneConfig config;
            if (command == "--preset")
            {
                config = preset_config(parse_choice<ScenePreset>(argv[2]));
            }
            else if (command == "--scene")
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
            const auto &[output, batch_size] = std::get<ImageOutput>(action);
            if (command != "--preset" && std::filesystem::exists(output) && std::filesystem::equivalent(input, output))
            {
                throw std::invalid_argument("Output image would overwrite the input model or scene");
            }
            const auto preview = load_preview(request);
            preview.scene.check_output_path(output);
            const PreviewRenderer renderer{preview.scene, request.lighting, request.mode};
            const auto image = renderer.render(preview.view, {}, batch_size);
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
