import std;
import formlight.preview;
import formlight.scene_toml;

namespace
{
using namespace formlight;

Vec3 rgb(LinearColor c)
{
    return {c.r, c.g, c.b};
}

void check_near(Vec3 a, Vec3 b, double tolerance, std::string_view label, PixelCoord pixel)
{
    const Vec3 error = abs_components(a - b);
    if (!(std::max({error.x, error.y, error.z}) <= tolerance))
    {
        throw std::runtime_error(
            std::format("{} at ({}, {}): difference ({}, {}, {})", label, pixel.x, pixel.y, error.x, error.y, error.z));
    }
}

void compare_replay(const PixelDiagnostic &path, const PixelDiagnostic &hybrid)
{
    if (path.samples.size() != hybrid.samples.size())
    {
        throw std::runtime_error("Primary sample counts differ");
    }
    for (std::size_t s = 0; s < path.samples.size(); ++s)
    {
        const auto &a = path.samples[s], &b = hybrid.samples[s];
        if (!a.surface || !b.surface || a.surface->hit.surface.id != b.surface->hit.surface.id ||
            a.lights.size() != b.lights.size() || a.lights.empty())
        {
            throw std::runtime_error("Primary rays and rasterization must select the same lit floor triangle");
        }
        Vec3 sum = rgb(a.surface->material.emission);
        for (std::size_t i = 0; i < a.lights.size(); ++i)
        {
            const auto &p = a.lights[i], &h = b.lights[i];
            if (p.kind != LightKind::Rectangle || p.scene_index != h.scene_index ||
                p.sampling_seed != h.sampling_seed || p.samples.size() != h.samples.size() || p.samples.empty())
            {
                throw std::runtime_error("Rectangle light or sampling parameters differ");
            }
            Vec3 direct{};
            for (std::size_t j = 0; j < p.samples.size(); ++j)
            {
                const auto &ps = p.samples[j], &hs = h.samples[j];
                if (!ps.position || ps.position != hs.position || ps.shadow.state != hs.shadow.state ||
                    ps.shadow.blocker != hs.shadow.blocker)
                {
                    throw std::runtime_error(std::format("Light {} sample {} at ({}, {}): position or blocker differs",
                                                         i, j, path.pixel.x, path.pixel.y));
                }
                check_near(ps.direct, hs.direct, 1e-10, "Light sample", path.pixel);
                direct = direct + ps.direct;
            }
            const Vec3 mean = direct / static_cast<double>(p.samples.size());
            check_near(p.direct, mean, 1e-12, "Mean direct light", path.pixel);
            sum = sum + mean;
        }
        check_near(rgb(a.color), sum, 2e-7, "Replayed radiance", path.pixel);
    }
    check_near(rgb(path.color), rgb(hybrid.color), 1e-7, "Resolved pixel", path.pixel);
}

void check_box(SceneConfig config)
{
    const auto request = prepare_render(config);
    const auto preview = load_preview(request);
    const PreviewRenderer hybrid{preview.scene, request.lighting, RenderMode::Hybrid};
    const PreviewRenderer path{preview.scene, request.lighting, RenderMode::Path};
    const auto frame = path.render(preview.view);
    for (const auto &[pixel, blocked] :
         {std::pair{PixelCoord{130, 282}, 64}, std::pair{PixelCoord{85, 310}, 35}, std::pair{PixelCoord{85, 395}, 0}})
    {
        const auto report = path.diagnose(preview.view, pixel);
        compare_replay(report, hybrid.diagnose(preview.view, pixel));
        const auto &samples = report.samples.front().lights.front().samples;
        if (samples.size() != 64 ||
            std::ranges::count_if(samples, [](const LightSample &s)
                                  { return s.shadow.state == ShadowState::Blocked; }) != blocked)
        {
            throw std::runtime_error("Box shadow differs from the calibrated umbra, penumbra or lit probe");
        }
        check_near(rgb(frame[pixel.y, pixel.x]), rgb(report.color), 0, "Rendered pixel", pixel);
    }

    auto edited = path;
    auto hybrid_edited = hybrid;
    auto &lights = config.lighting.value().lights;
    lights.insert(lights.begin(), LightSettings{.enabled = false});
    auto second = lights.back();
    second.center = {-1, 3, 1};
    second.shadows = false;
    lights.push_back(second);
    config.render.sampling = {7, 29};
    const auto lighting = prepare_render(config).lighting;
    edited.set_lighting(lighting);
    hybrid_edited.set_lighting(lighting);
    auto view = preview.view;
    view.ssaa = 2;
    const PixelCoord pixel{85, 310};
    const auto report = edited.diagnose(view, pixel);
    compare_replay(report, hybrid_edited.diagnose(view, pixel));
    const auto &records = report.samples.at(0).lights;
    if (report.samples.size() != 4 || records.size() != 2 || records[0].scene_index != 1 ||
        records[1].scene_index != 2 || records[1].samples.size() != 7 || records[1].sampling_seed != 29 ||
        records[1].samples.front().shadow.state != ShadowState::Disabled)
    {
        throw std::runtime_error("Relighting lost the edited rectangle settings");
    }
    check_near(rgb(path.diagnose(preview.view, pixel).color), rgb(frame[pixel.y, pixel.x]), 0, "Original frame", pixel);
}
} // namespace

int main(int argc, char **argv)
try
{
    if (argc != 2)
    {
        return 1;
    }
    check_box(load_scene_config(argv[1]));
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
