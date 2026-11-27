import std;
import formlight.preview;
import formlight.scene_toml;

namespace
{
using namespace formlight;

// With three bounces a floor path can enter the glass, leave the glass and hit the ceiling emitter.
PreviewView floor_view(double x)
{
    const PinholeCamera camera{{{x, 0.0001, 0.14}, {x, 0, 0.14}, {0, 0, 1}, 0.5}, {1, 1}};
    return {camera, {0.00001, 4}};
}

struct Errors
{
    double mean, rms;
};

Errors compare(const PreviewRenderer &renderer, PreviewView view, double reference)
{
    constexpr unsigned seeds = 64;
    double sum = 0, squared_error = 0;
    for (unsigned seed = 0; seed < seeds; ++seed)
    {
        view.path_sampling = {.samples = 128, .max_bounces = 3, .seed = seed, .method = BounceSampling::LightMixture};
        const double red = renderer.diagnose(view, {0, 0}).color.r;
        const double error = red - reference;
        sum += red;
        squared_error += error * error;
    }
    return {sum / seeds, std::sqrt(squared_error / seeds)};
}

void compare_targets(const SceneConfig &config)
{
    auto request = prepare_render(config);
    const auto with = load_preview(request);
    const PreviewRenderer targeted{with.scene, request.lighting, request.mode};
    if (!with.scene.instance(InstanceId{1}).sampling_target)
    {
        throw std::runtime_error("Cornell glass node must be an explicit direction target");
    }
    request.sampling_targets.clear();
    const auto without = load_preview(request);
    const PreviewRenderer baseline{without.scene, request.lighting, request.mode};

    double lamp_caustic_error = 0, targeted_caustic_error = 0;
    for (const double offset : {0.02, 0.12, 0.24, 0.48})
    {
        auto view = floor_view(-0.36 - offset);
        view.path_sampling = {.samples = 262144, .max_bounces = 3, .seed = 817, .method = BounceSampling::Cosine};
        const auto probe = baseline.diagnose(view, {0, 0});
        if (std::abs(probe.samples.at(0).surface.value().hit.view_depth - 0.0001) > 1e-10)
        {
            throw std::runtime_error("Caustic probe missed the floor");
        }
        const double reference = probe.color.r;
        const auto lamp = compare(baseline, view, reference);
        const auto both = compare(targeted, view, reference);
        std::println(
            "Floor offset {:.2f}, cosine reference {:.4f}, means {:.4f}/{:.4f}, RMS lamp {:.4f}, lamp + glass {:.4f}",
            offset, reference, lamp.mean, both.mean, lamp.rms, both.rms);
        if (!(reference > 0 && std::abs(lamp.mean - reference) < 0.06 && std::abs(both.mean - reference) < 0.06))
        {
            throw std::runtime_error("Mixed estimates disagree with the independent cosine reference");
        }
        if (offset < 0.34) // Inside the sphere's footprint. The fourth probe is an exterior control.
        {
            lamp_caustic_error += lamp.rms * lamp.rms;
            targeted_caustic_error += both.rms * both.rms;
        }
    }
    if (!(targeted_caustic_error < lamp_caustic_error))
    {
        throw std::runtime_error("Extra sphere sampling did not reduce aggregate error under the glass");
    }
}
} // namespace

int main(int argc, char **argv)
try
{
    if (argc != 2)
    {
        return 1;
    }
    compare_targets(load_scene_config(argv[1]));
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
