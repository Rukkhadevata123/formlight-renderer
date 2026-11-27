import std;
import formlight.preview;
import formlight.scene_toml;

namespace
{
using namespace formlight;

constexpr int column = 85, first_row = 270, last_row = 400;
struct Profile
{
    std::array<float, last_row - first_row + 1> red{};
    int transition_pixels{};
};

Profile measure(const GltfScene &scene, const PreviewView &view, const SceneConfig &config)
{
    const PreviewRenderer renderer{scene, prepare_render(config).lighting};
    const auto ground = scene.instances().back().id;
    Profile profile;
    for (int y = first_row; y <= last_row; ++y)
    {
        const auto pixel = renderer.diagnose(view, {column, y});
        const auto &sample = pixel.samples.at(0);
        if (!sample.surface || sample.surface->hit.surface.id.instance != ground)
        {
            throw std::runtime_error(std::format("Expected ground at ({}, {})", column, y));
        }
        const auto &rays = sample.lights.at(0).samples;
        const auto blocked = std::ranges::count_if(rays, [](const LightSample &ray)
                                                   { return ray.shadow.state == ShadowState::Blocked; });
        const double fraction = static_cast<double>(blocked) / rays.size();
        if ((y == first_row && fraction < 0.9) || (y == last_row && fraction > 0.1))
        {
            throw std::runtime_error("The ground scanline must span the shadow transition");
        }
        profile.transition_pixels += fraction >= 0.1 && fraction <= 0.9;
        profile.red[y - first_row] = pixel.color.r;
    }
    return profile;
}

double rms_error(const Profile &actual, const Profile &reference)
{
    double sum = 0;
    for (std::size_t i = 0; i < actual.red.size(); ++i)
    {
        const double difference = static_cast<double>(actual.red[i]) - reference.red[i];
        sum += difference * difference;
    }
    return std::sqrt(sum / actual.red.size());
}

int check_clearance(SceneConfig config, double lift)
{
    auto scene = GltfScene::load(config.scene.asset, GltfContent::Materials);
    if (!scene)
    {
        throw std::runtime_error(scene.error());
    }
    // In the box's reference frame, raising the box lowers the floor, camera and lamp by the same amount.
    auto floor_bounds = world_bounds(scene->instances());
    floor_bounds.low.y -= lift;
    floor_bounds.high.y -= lift;
    add_preview_ground(*scene, floor_bounds);
    config.camera.position.y -= lift;
    config.camera.target.y -= lift;
    auto &lamp = config.lighting.value().lights.at(0);
    lamp.center.y -= lift;
    const auto view = clip_to_bounds(prepare_render(config).camera.value(), world_bounds(scene->instances()));

    config.render.sampling = {1024, 23};
    lamp.width = 0.5;
    lamp.height = 0.5;
    const auto small = measure(*scene, view, config);
    lamp.width = 1.5;
    lamp.height = 1.5;
    const auto wide = measure(*scene, view, config);
    config.render.sampling = {4, 7};
    const double error4 = rms_error(measure(*scene, view, config), wide);
    config.render.sampling.samples = 64;
    const double error64 = rms_error(measure(*scene, view, config), wide);
    std::println("Lift {}: transition pixels (0.5 / 1.5 lamp) {} / {}; linear R RMS (4 / 64 samples) {:.6f} / {:.6f}",
                 lift, small.transition_pixels, wide.transition_pixels, error4, error64);
    if (!(small.transition_pixels > 0 && wide.transition_pixels > small.transition_pixels && error64 < error4))
    {
        throw std::runtime_error("Expected a wider penumbra with a larger lamp and lower RMS error with more samples");
    }
    return wide.transition_pixels;
}
} // namespace

int main(int argc, char **argv)
try
{
    if (argc != 2)
    {
        return 1;
    }
    const auto config = load_scene_config(argv[1]);
    const int touching = check_clearance(config, 0);
    const int lifted = check_clearance(config, 0.25);
    if (lifted <= touching)
    {
        throw std::runtime_error("Expected a wider ground transition after lifting the box");
    }
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
