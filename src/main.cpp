import std;
import formlight.gltf;

auto main(int argc, char **argv) -> int
{
    if (argc == 1 || (argc == 2 && std::string_view{argv[1]} == "--help"))
    {
        std::println("Usage: formlight-renderer [--help | --inspect MODEL.glb]");
        return 0;
    }

    if (argc == 3 && std::string_view{argv[1]} == "--inspect")
    {
        const auto scene = formlight::GltfGeometry::load_glb(argv[2]);
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

    std::cerr << "Unexpected arguments. Run formlight-renderer --help for usage.\n";
    return 1;
}
