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
    case ShadowState::PartiallyVisible:
        return std::format("partial shadow, transmitted RGB ({:.6g}, {:.6g}, {:.6g})", shadow.transmittance.x,
                           shadow.transmittance.y, shadow.transmittance.z);
    case ShadowState::Disabled:
        return "shadows off";
    case ShadowState::ZeroWeight:
        return "zero weight (shadow test skipped)";
    }
    std::unreachable();
}
void print_sample(const SampleDiagnostic &report)
{
    if (report.time)
    {
        std::println("Shutter time: {:.9g}", *report.time);
    }
    if (const auto &origin = report.lens_origin)
    {
        std::println("Lens origin: ({:.9g}, {:.9g}, {:.9g})", origin->x, origin->y, origin->z);
    }
    if (const auto &surface = report.surface)
    {
        const auto &id = surface->hit.surface.id;
        std::println("Surface: instance {}, primitive {}, material slot {}, depth {:.9g}",
                     std::to_underlying(id.instance), std::to_underlying(id.primitive), surface->material_index,
                     surface->hit.view_depth);
        const auto &[base, emission, metallic, roughness, occlusion, transmission] = surface->material;
        if (const auto &n = surface->shading_normal)
        {
            std::println("World shading normal: ({}, {}, {})", n->x, n->y, n->z);
        }
        if (surface->metal_fuzz)
        {
            std::println("Path metal fuzz: {}", *surface->metal_fuzz);
        }
        else
        {
            std::println("Metallic: {}  Roughness: {}", metallic, roughness);
        }
        if (const auto *triangle = std::get_if<TriangleCoordinates>(&surface->hit.surface.coordinates))
        {
            const auto weights = triangle->weights;
            std::println("Barycentrics: ({:.9g}, {:.9g}, {:.9g})", weights.a, weights.b, weights.c);
        }
        else
        {
            const auto &point = std::get<SphereCoordinates>(surface->hit.surface.coordinates);
            const auto uv = sphere_uv(point.outward_normal);
            std::println("Analytic sphere {}, UV ({:.9g}, {:.9g})", point.index, uv.u, uv.v);
        }
        std::println("Base linear RGB: ({}, {}, {})", base.r, base.g, base.b);
        std::println("Surface emission (linear RGB): ({}, {}, {})", emission.r, emission.g, emission.b);
        std::println("glTF transmission: {}", transmission);
        if (const auto &through = surface->transmitted)
        {
            std::println("Transmitted background (linear RGB): ({}, {}, {})", through->x, through->y, through->z);
        }
        const auto ambient = surface->ambient;
        std::println("Occlusion: {}  Surface ambient (linear RGB): ({}, {}, {})", occlusion, ambient.x, ambient.y,
                     ambient.z);
        if (const auto &bounce = report.bounce_radiance)
        {
            std::println("Path bounces (linear RGB): ({}, {}, {})", bounce->x, bounce->y, bounce->z);
        }
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
    else if (report.medium)
    {
        const auto &event = *report.medium;
        const auto albedo = event.scattering_albedo;
        std::println("{} in medium {} at ray t={:.9g}, scattering albedo ({:.6g}, {:.6g}, {:.6g})",
                     albedo == Vec3{} ? "Absorbed" : "Collision", event.index, event.t, albedo.x, albedo.y, albedo.z);
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
