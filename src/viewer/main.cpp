import std;
import formlight.viewer.session;
import formlight.scene_toml;
import formlight.viewer.window;

namespace
{
using namespace formlight;

void view_scene(const std::filesystem::path &path)
{
    const auto title = std::format("Formlight | {}", path.filename().string());
    ViewerWindow window{title.c_str()};
    ViewerSession session{load_scene_config(path)};
    while (window.poll())
    {
        window.draw(session);
    }
}
} // namespace

int main(int argc, char **argv)
{
    if (argc == 1 || (argc == 2 && std::string_view{argv[1]} == "--help"))
    {
        std::println("Usage: formlight-viewer --scene SCENE.toml");
        return 0;
    }
    if (argc != 3 || std::string_view{argv[1]} != "--scene")
    {
        std::cerr << "Expected --scene SCENE.toml\n";
        return 1;
    }
    try
    {
        view_scene(argv[2]);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
