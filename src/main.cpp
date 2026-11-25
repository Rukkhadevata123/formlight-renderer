import std;
import formlight.base_color_preview;
import formlight.image_codec;

auto main(int argc, char **argv) -> int
{
    if (argc == 1 || (argc == 2 && std::string_view{argv[1]} == "--help"))
    {
        std::println("Usage: formlight-renderer --inspect MODEL.glb\n"
                     "       formlight-renderer --render MODEL.glb --output IMAGE.png");
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

    if (argc == 5 && std::string_view{argv[1]} == "--render" && std::string_view{argv[3]} == "--output")
    {
        using namespace formlight;
        const std::filesystem::path input = argv[2], output = argv[4];
        const auto scene = GltfScene::load_glb(input, GltfContent::BaseColor);
        if (!scene)
        {
            std::cerr << scene.error() << '\n';
            return 1;
        }
        try
        {
            if (std::filesystem::exists(output) && std::filesystem::equivalent(input, output))
            {
                throw std::invalid_argument("Input model and output image refer to the same file");
            }
            constexpr ImageExtent extent{640, 480};
            const auto image = render_base_color(*scene, frame_scene(*scene, extent));
            write_png(output, image);
            std::println("Wrote {} ({} x {})", output.string(), extent.width, extent.height);
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
