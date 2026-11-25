import std;
import formlight.scene_toml;
import formlight.preview;

namespace
{
void round_trip(const std::filesystem::path &source, const std::filesystem::path &destination)
{
    using namespace formlight;
    auto config = load_scene_config(source);
    config.render.extent = {48, 32};
    config.render.sampling = {4, std::numeric_limits<std::uint64_t>::max()};
    config.camera.auto_frame = false;
    config.camera.position = {2, 1, 3};
    config.camera.target = {0, -0.2, 0};
    config.light.width = 0.75;

    const auto before = render_scene(prepare_render(config));
    save_scene_config(destination, config);
    const auto loaded = load_scene_config(destination);
    if (!std::filesystem::equivalent(config.scene.asset, loaded.scene.asset) ||
        loaded.render.sampling.seed != config.render.sampling.seed || loaded.camera.auto_frame)
    {
        throw std::runtime_error("Save As lost the asset reference, camera, or full-width seed");
    }
    const auto after = render_scene(prepare_render(loaded));
    if (after.extent().width != before.extent().width || after.extent().height != before.extent().height)
    {
        throw std::runtime_error("Image extent changed after Save As");
    }
    for (int y = 0; y < before.extent().height; ++y)
    {
        for (int x = 0; x < before.extent().width; ++x)
        {
            const auto a = before[y, x], b = after[y, x];
            if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a)
            {
                throw std::runtime_error("Render from the edited config differs from its TOML round trip");
            }
        }
    }
}
} // namespace

auto main(int argc, char **argv) -> int
{
    if (argc != 2)
    {
        return 1;
    }
    const auto scratch = std::filesystem::current_path() / std::format("config-test-{:08x}", std::random_device{}());
    if (!std::filesystem::create_directory(scratch))
    {
        return 1;
    }
    const auto saved = scratch / "saved.toml";
    int status = 0;
    try
    {
        round_trip(argv[1], saved);
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        status = 1;
    }
    std::filesystem::remove(saved);
    std::filesystem::remove(scratch);
    return status;
}
