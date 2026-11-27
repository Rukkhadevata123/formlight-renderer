module formlight.cli;

import std;
import formlight.preview;

namespace formlight
{
namespace
{
std::string describe_shadow(const ShadowResult &shadow)
{
    switch (shadow.state)
    {
    case ShadowState::Blocked:
        if (shadow.blocker)
        {
            return std::format("blocked by instance {} primitive {}", std::to_underlying(shadow.blocker->instance),
                               std::to_underlying(shadow.blocker->primitive));
        }
        return "blocked by shadow map";
    case ShadowState::Visible:
        return "visible";
    case ShadowState::Disabled:
        return "shadows off";
    case ShadowState::ZeroWeight:
        return "zero weight (shadow test skipped)";
    }
    std::unreachable();
}
} // namespace

void print_pixel_report(const PixelDiagnostic &report)
{
    const auto center = pixel_center(report.pixel.x, report.pixel.y);
    std::println("Pixel ({}, {}) at sample ({}, {})", report.pixel.x, report.pixel.y, center.x, center.y);
    if (const auto &surface = report.surface)
    {
        const auto &[id, weights] = surface->hit.surface;
        std::println("Surface: instance {}, primitive {}, material slot {}, depth {:.9g}",
                     std::to_underlying(id.instance), std::to_underlying(id.primitive), surface->material_index,
                     surface->hit.view_depth);
        const auto &[base, emission, metallic, roughness, occlusion] = surface->material;
        std::println("Metallic: {}  Roughness: {}", metallic, roughness);
        std::println("Barycentrics: ({:.9g}, {:.9g}, {:.9g})  Base linear RGB: ({}, {}, {})", weights.a, weights.b,
                     weights.c, base.r, base.g, base.b);
        std::println("Surface emission (linear RGB): ({}, {}, {})", emission.r, emission.g, emission.b);
        const auto ambient = surface->ambient;
        std::println("Occlusion: {}  Surface ambient (linear RGB): ({}, {}, {})", occlusion, ambient.x, ambient.y,
                     ambient.z);
        if (base.a < 1)
        {
            std::println("Front layer opacity: {}", base.a);
        }
        if (report.light == LightKind::Rectangle)
        {
            const auto blocked = std::ranges::count_if(report.samples, [](const auto &sample)
                                                       { return sample.shadow.state == ShadowState::Blocked; });
            std::println("Rectangle light: {} samples, {} blocked, seed {}", report.samples.size(), blocked,
                         report.sampling_seed);
        }
        else if (report.light == LightKind::Directional)
        {
            std::println("Directional light");
        }
        for (std::size_t i = 0; i < report.samples.size(); ++i)
        {
            const auto &sample = report.samples[i];
            const auto point = sample.position ? std::format(" at ({:.7g}, {:.7g}, {:.7g})", sample.position->x,
                                                             sample.position->y, sample.position->z)
                                               : std::string{};
            const auto rgb = report.sample_contribution(i);
            std::println("  [{}]{} {} | linear RGB contribution ({:.9g}, {:.9g}, {:.9g})", i, point,
                         describe_shadow(sample.shadow), rgb.x, rgb.y, rgb.z);
        }
    }
    else
    {
        std::println("Background");
    }
    std::println("Pixel linear RGB: ({}, {}, {})", report.color.r, report.color.g, report.color.b);
}
} // namespace formlight
