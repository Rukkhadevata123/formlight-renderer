module;

#include <oneapi/tbb/blocked_range2d.h>
#include <oneapi/tbb/parallel_for.h>

module formlight.preview;

import std;
import formlight.sampling;

namespace formlight
{
PathFrame::PathFrame(const PreviewRenderer &renderer, const PreviewView &view)
    : renderer_(renderer), grid_(sample_grid_view(view)), ssaa_(view.ssaa)
{
    if (!renderer_.path_)
    {
        throw std::invalid_argument("A path frame requires path rendering mode");
    }
}

ColorImage PathFrame::render_batch(std::uint32_t count, std::stop_token stop)
{
    if (count == 0 || count > std::numeric_limits<std::uint32_t>::max() - completed_)
    {
        throw std::invalid_argument("Path batch size must be positive and fit the remaining sample range");
    }
    if (stop.stop_requested())
    {
        throw std::runtime_error("Rendering cancelled");
    }
    auto grid = grid_;
    grid.path_sampling.samples = completed_ + count;
    const auto extent = grid.camera.extent();
    // Work on a private prefix. Failed tiles cannot advance any pixel of the published frame.
    const auto size = static_cast<std::size_t>(extent.width) * extent.height;
    auto next = sums_ ? std::make_shared<std::vector<RayRadianceSum>>(*sums_)
                      : std::make_shared<std::vector<RayRadianceSum>>(size);
    const ImageExtent output{extent.width / ssaa_, extent.height / ssaa_};
    ColorImage image{output};
    const auto trace_tile = [&](const oneapi::tbb::blocked_range2d<int> &tile)
    {
        for (int y = tile.rows().begin(); y < tile.rows().end(); ++y)
        {
            for (int x = tile.cols().begin(); x < tile.cols().end(); ++x)
            {
                const auto trace = [&](int i)
                {
                    const PixelCoord pixel{x * ssaa_ + i % ssaa_, y * ssaa_ + i / ssaa_};
                    auto &sum = (*next)[static_cast<std::size_t>(pixel.y) * extent.width + pixel.x];
                    return renderer_.trace_primary(grid, pixel, completed_, sum, stop);
                };
                image[y, x] = ssaa_ == 1 ? trace(0) : resolve_samples(ssaa_ * ssaa_, trace);
            }
        }
    };
    oneapi::tbb::task_group_context tasks{oneapi::tbb::task_group_context::isolated};
    oneapi::tbb::parallel_for(oneapi::tbb::blocked_range2d<int>{0, output.height, 8, 0, output.width, 8}, trace_tile,
                              tasks);
    if (stop.stop_requested())
    {
        throw std::runtime_error("Rendering cancelled");
    }
    sums_ = std::move(next);
    completed_ += count;
    return image;
}

LinearColor PreviewRenderer::trace_primary(const PreviewView &view, PixelCoord pixel, std::uint32_t first_sample,
                                           RayRadianceSum &sum, std::stop_token stop,
                                           std::vector<SampleDiagnostic> *records, int ssaa) const
{
    const auto center = pixel_center(pixel.x, pixel.y);
    const Ray pinhole = view.camera.ray_for_sample(center);
    const auto pixel_index = static_cast<std::uint64_t>(pixel.y) * view.camera.extent().width + pixel.x;
    const bool lens_open = view.lens.radius() > 0;
    const bool sample_time = view.shutter.open < view.shutter.close;
    const bool sample_primary = lens_open || sample_time || !scene_.media.empty();
    const auto count = view.path_sampling.samples;
    if (count == 0)
    {
        throw std::invalid_argument("Path sample count must be positive");
    }
    const auto trace = [&](std::uint32_t i, RayRadianceSum &ray_sum)
    {
        if (stop.stop_requested())
        {
            throw std::runtime_error("Rendering cancelled");
        }
        Ray ray = pinhole;
        auto path_key = pixel_index;
        auto sampling = view.path_sampling;
        ray.time = view.shutter.open;
        if (sample_primary)
        {
            PathSampler camera_sampler{sampling.seed, pixel_index, i};
            if (lens_open)
            {
                const double u = camera_sampler.uniform();
                ray = view.camera.ray_from_lens(ray, view.lens, u, camera_sampler.uniform());
            }
            if (sample_time)
            {
                ray.time = std::lerp(view.shutter.open, view.shutter.close, camera_sampler.uniform());
            }
            // Each camera sample gets its own first hit, light samples and one continuation path.
            path_key = camera_sampler.next_bits();
            sampling.samples = 1;
        }
        SampleDiagnostic *record = nullptr;
        if (records)
        {
            record =
                &records->emplace_back(SampleDiagnostic{.position = {center.x / ssaa, center.y / ssaa},
                                                        .weight = 1.0 / (ssaa * ssaa) / (sample_primary ? count : 1u),
                                                        .time = ray.time});
            if (lens_open)
            {
                record->lens_origin = ray.origin;
            }
        }
        const auto color =
            trace_camera_ray(view, ray, path_key, sampling, sample_primary ? 0u : first_sample, ray_sum, stop, record);
        if (record)
        {
            record->color = color;
        }
        return color;
    };
    if (!sample_primary)
    {
        return trace(first_sample, sum);
    }
    for (auto i = first_sample; i < count; ++i)
    {
        RayRadianceSum ray_sum;
        const auto color = trace(i, ray_sum);
        sum.add(color);
    }
    return sum.mean(count);
}

LinearColor PreviewRenderer::trace_camera_ray(const PreviewView &view, const Ray &ray, std::uint64_t path_key,
                                              const PathSampling &sampling, std::uint32_t first_sample,
                                              RayRadianceSum &sum, std::stop_token stop, SampleDiagnostic *record) const
{
    const double depth_per_unit = view.camera.forward_component(ray.direction);
    const double origin_depth = view.lens.radius() > 0 ? view.camera.to_view(ray.origin).depth : 0;
    // The camera depth range includes both endpoints; ray queries have open intervals.
    const RayInterval interval{std::nextafter((view.depth.near_depth - origin_depth) / depth_per_unit, 0.0),
                               std::nextafter((view.depth.far_depth - origin_depth) / depth_per_unit,
                                              std::numeric_limits<double>::infinity())};
    std::optional<PathHit> first;
    const auto color = path_->accumulate(ray, interval, sum, first_sample, sampling, path_key, view.area_light_pattern,
                                         stop, record ? &first : nullptr, record ? &record->medium : nullptr);
    if (record && first)
    {
        const auto &hit = first->intersection;
        const auto id = hit.surface.id;
        const auto *sphere = std::get_if<SphereCoordinates>(&hit.surface.coordinates);
        const auto material_index =
            sphere ? scene_.spheres()[sphere->index].material_index
                   : scene_.instance(id.instance).triangles[std::to_underlying(id.primitive)].material_index;
        record->surface = DiagnosticSurface{{hit.surface, origin_depth + hit.t * depth_per_unit, first->back_face},
                                            material_index,
                                            first->material,
                                            {},
                                            first->shading_normal};
        const auto &material = scene_.material(material_index);
        if (material.path_surface == PathSurface::Metal)
        {
            record->surface->metal_fuzz = material.fuzz;
        }
        record->lights = std::move(first->lights);
        record->bounce_radiance = first->bounce_radiance;
    }
    return color;
}

} // namespace formlight
