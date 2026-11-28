module;

#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/task_group.h>

module formlight.preview;

import std;

namespace formlight
{
PreviewView sample_grid_view(const PreviewView &view)
{
    const auto extent = sample_grid_extent(view.camera.extent(), view.ssaa);
    if (view.ssaa == 1)
    {
        return view;
    }
    return {PinholeCamera{view.camera.view(), extent},
            view.depth,
            view.area_light_pattern,
            1,
            view.path_sampling,
            view.lens,
            view.shutter};
}

namespace
{
constexpr LinearColor background{0.035f, 0.045f, 0.06f, 1.0f};

struct LayerFragment
{
    int column;
    RasterHit hit;
};
using LayerRows = std::vector<std::vector<LayerFragment>>;

enum class SurfacePass
{
    Solid,
    Layer,
    Cull
};

SurfacePass surface_pass(const GltfScene &scene, const RasterTriangle &piece)
{
    const auto id = piece.source();
    const auto &instance = scene.instance(id.instance);
    const auto &face = instance.triangles[std::to_underlying(id.primitive)];
    const auto &material = scene.material(face);
    if (piece.back_face() && !material.double_sided)
    {
        return SurfacePass::Cull;
    }
    return material.alpha_mode == AlphaMode::Blend || material.transmission_factor > 0 ? SurfacePass::Layer
                                                                                       : SurfacePass::Solid;
}

bool in_front_of_solid(const GltfScene &scene, const RasterHit &hit, const std::optional<RasterHit> &solid)
{
    if (!solid)
    {
        return true;
    }
    if (hit.view_depth != solid->view_depth)
    {
        return hit.view_depth < solid->view_depth;
    }
    // BLEND fragments require strictly nearer depth. OPAQUE and MASK retain the solid pass's ID tie-break.
    const auto id = hit.surface.id;
    const auto &face = scene.instance(id.instance).triangles[std::to_underlying(id.primitive)];
    return id < solid->surface.id && scene.material(face).alpha_mode != AlphaMode::Blend;
}

LayerRows rasterize_visibility(const GltfScene &scene, const PreviewView &view, const SurfaceFilter &solid_filter,
                               VisibilityBuffer visible, std::stop_token stop)
{
    const auto pieces = RasterTriangle::prepare(view.camera, scene.instances(), view.depth, stop);
    std::vector<const RasterTriangle *> layered;
    for (const auto &piece : pieces)
    {
        if (stop.stop_requested())
        {
            throw std::runtime_error("Rendering cancelled");
        }
        const auto pass = surface_pass(scene, piece);
        if (pass == SurfacePass::Solid)
        {
            piece.rasterize(visible, solid_filter);
        }
        else if (pass == SurfacePass::Layer)
        {
            layered.push_back(&piece);
        }
    }

    if (layered.empty())
    {
        return {};
    }
    const auto extent = view.camera.extent();
    LayerRows rows(extent.height);
    for (const auto *piece : layered)
    {
        if (stop.stop_requested())
        {
            throw std::runtime_error("Rendering cancelled");
        }
        const auto collect = [&](int x, int y, const RasterHit &hit)
        {
            if (in_front_of_solid(scene, hit, visible[y, x]))
            {
                rows[y].push_back({x, hit});
            }
        };
        piece->for_each_hit(extent, collect);
    }
    return rows;
}

void composite_layers(std::span<LinearColor> row, std::span<LayerFragment> fragments, auto shade)
{
    // Sort each pixel back to front. At equal depth the smaller surface ID is on top.
    const auto back_to_front = [](const LayerFragment &a, const LayerFragment &b)
    {
        if (a.column != b.column)
        {
            return a.column < b.column;
        }
        return std::tie(a.hit.view_depth, a.hit.surface.id) > std::tie(b.hit.view_depth, b.hit.surface.id);
    };
    std::ranges::sort(fragments, back_to_front);
    for (const auto &[x, hit] : fragments)
    {
        auto &dst = row[x];
        const auto src = shade(x, hit, dst);
        if (src.a > 0)
        {
            // The background and depth-writing surfaces are opaque, so the composite alpha stays one.
            dst = {std::lerp(dst.r, src.r, src.a), std::lerp(dst.g, src.g, src.a), std::lerp(dst.b, src.b, src.a), 1};
        }
    }
}
} // namespace

ColorImage PreviewRenderer::render(const PreviewView &view, std::stop_token stop, std::uint32_t path_batch_size) const
{
    if (path_)
    {
        PathFrame frame{*this, view};
        const auto target = view.path_sampling.samples;
        for (;;)
        {
            auto image = frame.render_batch(std::min(path_batch_size, target - frame.completed_samples()), stop);
            if (frame.completed_samples() == target)
            {
                return image;
            }
        }
    }
    auto samples = render_sample_grid(sample_grid_view(view), stop);
    if (view.ssaa == 1)
    {
        return samples;
    }
    const auto extent = view.camera.extent();
    ColorImage resolved{extent};
    for (int y = 0; y < extent.height; ++y)
    {
        if (stop.stop_requested())
        {
            throw std::runtime_error("Rendering cancelled");
        }
        for (int x = 0; x < extent.width; ++x)
        {
            resolved[y, x] = resolve_samples(4, [&](int i) { return samples[2 * y + i / 2, 2 * x + i % 2]; });
        }
    }
    return resolved;
}

ColorImage PreviewRenderer::render_sample_grid(const PreviewView &view, std::stop_token stop) const
{
    const ImageExtent extent = view.camera.extent();
    // Only the render's stop token may interrupt the frame; a cancelled outer TBB task could skip rows.
    oneapi::tbb::task_group_context frame_tasks{oneapi::tbb::task_group_context::isolated};
    ColorImage image{extent, background};
    std::vector<std::optional<RasterHit>> pixels(static_cast<std::size_t>(extent.width) * extent.height);
    const VisibilityBuffer visible{pixels.data(), extent.height, extent.width};
    auto layers = rasterize_visibility(scene_, view, solid_filter_, visible, stop);
    const auto shade_row = [&](int y)
    {
        const auto shade_hit = [&](int x, const RasterHit &hit, LinearColor behind)
        { return shade(hit, {x, y}, view, behind, stop); };
        if (stop.stop_requested())
        {
            throw std::runtime_error("Rendering cancelled");
        }
        for (int x = 0; x < extent.width; ++x)
        {
            if (const auto &hit = visible[y, x])
            {
                image[y, x] = shade_hit(x, *hit, background);
            }
        }
        if (!layers.empty())
        {
            composite_layers({&image[y, 0], static_cast<std::size_t>(extent.width)}, layers[y], shade_hit);
        }
    };
    oneapi::tbb::parallel_for(0, extent.height, shade_row, frame_tasks);
    return image;
}

PixelDiagnostic PreviewRenderer::diagnose(const PreviewView &view, PixelCoord pixel, std::stop_token stop) const
{
    const ImageExtent extent = view.camera.extent();
    if (pixel.x < 0 || pixel.x >= extent.width || pixel.y < 0 || pixel.y >= extent.height)
    {
        throw std::out_of_range(
            std::format("Pixel ({}, {}) is outside the {} x {} image", pixel.x, pixel.y, extent.width, extent.height));
    }
    const auto grid = sample_grid_view(view);
    const auto pieces = path_ ? std::vector<RasterTriangle>{}
                              : RasterTriangle::prepare(grid.camera, scene_.instances(), grid.depth, stop);
    const int axis = view.ssaa;
    const int count = axis * axis;
    PixelDiagnostic report{.pixel = pixel, .samples = {}, .color = {}};
    report.samples.reserve(count);
    report.color = resolve_samples(
        count,
        [&](int i)
        {
            const PixelCoord sample{pixel.x * axis + i % axis, pixel.y * axis + i / axis};
            if (path_)
            {
                RayRadianceSum sum;
                return trace_primary(grid, sample, 0, sum, stop, &report.samples, axis);
            }
            const auto center = pixel_center(sample.x, sample.y);
            auto &record = report.samples.emplace_back(SampleDiagnostic{
                .position = {center.x / axis, center.y / axis}, .weight = 1.0 / count, .color = background});
            diagnose_sample(grid, sample, pieces, record, stop);
            return record.color;
        });
    return report;
}

void PreviewRenderer::diagnose_sample(const PreviewView &view, PixelCoord pixel, std::span<const RasterTriangle> pieces,
                                      SampleDiagnostic &report, std::stop_token stop) const
{
    const auto center = pixel_center(pixel.x, pixel.y);
    std::optional<RasterHit> solid;
    std::vector<LayerFragment> layers;
    for (const auto &piece : pieces)
    {
        if (stop.stop_requested())
        {
            throw std::runtime_error("Pixel inspection cancelled");
        }
        if (const auto hit = piece.sample(center))
        {
            const auto pass = surface_pass(scene_, piece);
            if (pass == SurfacePass::Solid)
            {
                hit->keep_nearest(solid, solid_filter_);
            }
            else if (pass == SurfacePass::Layer)
            {
                layers.push_back({0, *hit});
            }
        }
    }

    const auto shade_hit = [&](int, const RasterHit &hit, LinearColor behind)
    { return shade(hit, pixel, view, behind, stop, &report); };
    if (solid)
    {
        report.color = shade_hit(0, *solid, background);
    }
    std::erase_if(layers, [&](const auto &fragment) { return !in_front_of_solid(scene_, fragment.hit, solid); });
    composite_layers({&report.color, 1}, layers, shade_hit);
}
} // namespace formlight
