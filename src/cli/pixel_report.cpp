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
void print_sample(const SampleDiagnostic &report)
{
    if (const auto &surface = report.surface)
    {
        const auto &id = surface->hit.surface.id;
        const auto weights = surface->hit.surface.weights;
        std::println("Surface: instance {}, primitive {}, material slot {}, depth {:.9g}",
                     std::to_underlying(id.instance), std::to_underlying(id.primitive), surface->material_index,
                     surface->hit.view_depth);
        const auto &[base, emission, metallic, roughness, occlusion] = surface->material;
        if (const auto &n = surface->shading_normal)
        {
            std::println("World shading normal: ({}, {}, {})", n->x, n->y, n->z);
        }
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
        for (std::size_t index = 0; index < report.lights.size(); ++index)
        {
            const auto &light = report.lights[index];
            std::println("Light {} surface direct RGB: ({}, {}, {})", light.scene_index, light.direct.x, light.direct.y,
                         light.direct.z);
            if (light.kind == LightKind::Rectangle)
            {
                const auto blocked = std::ranges::count_if(light.samples, [](const auto &sample)
                                                           { return sample.shadow.state == ShadowState::Blocked; });
                std::println("Rectangle light: {} samples, {} blocked, seed {}", light.samples.size(), blocked,
                             light.sampling_seed);
            }
            else if (light.kind == LightKind::Directional)
            {
                std::println("Directional light");
            }
            else if (light.kind == LightKind::Point)
            {
                std::println("Point light");
            }
            for (std::size_t i = 0; i < light.samples.size(); ++i)
            {
                const auto &sample = light.samples[i];
                const auto point = sample.position ? std::format(" at ({:.7g}, {:.7g}, {:.7g})", sample.position->x,
                                                                 sample.position->y, sample.position->z)
                                                   : std::string{};
                const auto rgb = report.sample_contribution(index, i);
                std::println("  [{}]{} {} | linear RGB contribution ({:.9g}, {:.9g}, {:.9g})", i, point,
                             describe_shadow(sample.shadow), rgb.x, rgb.y, rgb.z);
            }
        }
    }
    else
    {
        std::println("Background");
    }
}
} // namespace

void print_pixel_report(const PixelDiagnostic &report)
{
    if (report.samples.size() == 1)
    {
        const auto center = report.samples.front().position;
        std::println("Pixel ({}, {}) at sample ({}, {})", report.pixel.x, report.pixel.y, center.x, center.y);
    }
    else
    {
        std::println("Pixel ({}, {})", report.pixel.x, report.pixel.y);
    }
    for (const auto &sample : report.samples)
    {
        if (report.samples.size() > 1)
        {
            std::println("Sample ({}, {}), weight {}", sample.position.x, sample.position.y, sample.weight);
        }
        print_sample(sample);
        if (report.samples.size() > 1)
        {
            std::println("Sample linear RGB: ({}, {}, {})", sample.color.r, sample.color.g, sample.color.b);
        }
    }
    std::println("Pixel linear RGB: ({}, {}, {})", report.color.r, report.color.g, report.color.b);
}
} // namespace formlight
