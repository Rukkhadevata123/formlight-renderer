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

void sort_layers(std::span<LayerFragment> fragments)
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
}

void blend_over(LinearColor &dst, LinearColor src)
{
    if (src.a > 0)
    {
        dst = {std::lerp(dst.r, src.r, src.a), std::lerp(dst.g, src.g, src.a), std::lerp(dst.b, src.b, src.a), 1};
    }
}

void composite_layers(std::span<LinearColor> row, std::span<LayerFragment> fragments, auto shade)
{
    sort_layers(fragments);
    for (const auto &[x, hit] : fragments)
    {
        auto &dst = row[x];
        blend_over(dst, shade(x, hit, dst));
    }
}

struct LayerPixel
{
    PixelCoord pixel;
    const RasterHit *hit;
    LinearColor shaded{};
};

void composite_rough_layers(ColorImage &image, VisibilityBuffer visible, LayerRows &rows, int axis,
                            std::stop_token stop, oneapi::tbb::task_group_context &tasks, auto shade)
{
    // Each pass contributes at most one fragment per pixel. Finish all reads before publishing this layer.
    std::vector<std::vector<LayerPixel>> passes;
    const auto [width, height] = image.extent();
    for (int y = 0; y < height; ++y)
    {
        if (stop.stop_requested())
        {
            throw std::runtime_error("Rendering cancelled");
        }
        auto &row = rows[y];
        sort_layers(row);
        int column = -1;
        std::size_t rank = 0;
        for (const auto &fragment : row)
        {
            if (column != fragment.column)
            {
                column = fragment.column;
                rank = 0;
            }
            if (rank == passes.size())
            {
                passes.emplace_back();
            }
            passes[rank++].push_back({{column, y}, &fragment.hit});
        }
    }
    std::vector<double> depths(static_cast<std::size_t>(width) * height, std::numeric_limits<double>::infinity());
    for (std::size_t i = 0; i < depths.size(); ++i)
    {
        if (const auto &hit = visible.data_handle()[i])
        {
            depths[i] = hit->view_depth;
        }
    }
    // The largest half-width is 1/48 of the shorter image dimension, capped at 16 output pixels.
    const TransmissionBackdrop backdrop{image, depths, std::min(16.0 * axis, std::min(width, height) / 48.0)};
    for (auto &pass : passes)
    {
        oneapi::tbb::parallel_for(
            std::size_t{0}, pass.size(),
            [&](std::size_t i)
            {
                if (stop.stop_requested())
                {
                    throw std::runtime_error("Rendering cancelled");
                }
                auto &[pixel, hit, shaded] = pass[i];
                shaded = shade(pixel, *hit, image[pixel.y, pixel.x], &backdrop);
            },
            tasks);
        for (const auto &[pixel, hit, shaded] : pass)
        {
            if (shaded.a > 0)
            {
                blend_over(image[pixel.y, pixel.x], shaded);
                depths[static_cast<std::size_t>(pixel.y) * width + pixel.x] = hit->view_depth;
            }
        }
    }
}
} // namespace

LinearColor TransmissionBackdrop::filtered(PixelCoord pixel, double depth, double roughness) const
{
    const auto [width, height] = image.extent();
    const double radius = roughness * roughness * max_radius;
    const int reach = static_cast<int>(std::ceil(radius));
    Vec3 sum{};
    double total{};
    for (int y = std::max(0, pixel.y - reach); y <= std::min(height - 1, pixel.y + reach); ++y)
    {
        const double wy = std::min(1.0, 1 + radius - std::abs(y - pixel.y));
        for (int x = std::max(0, pixel.x - reach); x <= std::min(width - 1, pixel.x + reach); ++x)
        {
            if ((x != pixel.x || y != pixel.y) && depths[static_cast<std::size_t>(y) * width + x] <= depth)
            {
                continue;
            }
            const auto color = image[y, x];
            const double weight = wy * std::min(1.0, 1 + radius - std::abs(x - pixel.x));
            sum = sum + Vec3{color.r, color.g, color.b} * weight;
            total += weight;
        }
    }
    const Vec3 mean = sum / total; // The center tap always has unit weight.
    return {static_cast<float>(mean.x), static_cast<float>(mean.y), static_cast<float>(mean.z), 1};
}

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
    auto samples = render_sample_grid(sample_grid_view(view), stop, nullptr, view.ssaa);
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

ColorImage PreviewRenderer::render_sample_grid(const PreviewView &view, std::stop_token stop, PixelDiagnostic *report,
                                               int axis) const
{
    const ImageExtent extent = view.camera.extent();
    // Only the render's stop token may interrupt the frame; a cancelled outer TBB task could skip rows.
    oneapi::tbb::task_group_context frame_tasks{oneapi::tbb::task_group_context::isolated};
    ColorImage image{extent, background};
    std::vector<std::optional<RasterHit>> pixels(static_cast<std::size_t>(extent.width) * extent.height);
    const VisibilityBuffer visible{pixels.data(), extent.height, extent.width};
    auto layers = rasterize_visibility(scene_, view, solid_filter_, visible, stop);
    const auto shade_at =
        [&](PixelCoord pixel, const RasterHit &hit, LinearColor behind, const TransmissionBackdrop *backdrop)
    {
        SampleDiagnostic *record = nullptr;
        if (report && pixel.x / axis == report->pixel.x && pixel.y / axis == report->pixel.y)
        {
            record = &report->samples[(pixel.y % axis) * axis + pixel.x % axis];
        }
        return shade(hit, pixel, view, behind, stop, record, backdrop);
    };
    const auto shade_row = [&](int y)
    {
        const auto shade_hit = [&](int x, const RasterHit &hit, LinearColor behind)
        { return shade_at({x, y}, hit, behind, nullptr); };
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
        if (!rough_transmission_ && !layers.empty())
        {
            composite_layers({&image[y, 0], static_cast<std::size_t>(extent.width)}, layers[y], shade_hit);
        }
    };
    oneapi::tbb::parallel_for(0, extent.height, shade_row, frame_tasks);
    if (rough_transmission_ && !layers.empty())
    {
        composite_rough_layers(image, visible, layers, axis, stop, frame_tasks, shade_at);
    }
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
    const int axis = view.ssaa;
    const int count = axis * axis;
    PixelDiagnostic report{.pixel = pixel, .samples = {}, .color = {}};
    report.samples.reserve(count);
    if (!path_ && rough_transmission_)
    {
        for (int i = 0; i < count; ++i)
        {
            const auto center = pixel_center(pixel.x * axis + i % axis, pixel.y * axis + i / axis);
            report.samples.push_back(
                SampleDiagnostic{.position = {center.x / axis, center.y / axis}, .weight = 1.0 / count});
        }
        const auto frame = render_sample_grid(grid, stop, &report, axis);
        report.color = resolve_samples(
            count, [&](int i)
            { return report.samples[i].color = frame[pixel.y * axis + i / axis, pixel.x * axis + i % axis]; });
        return report;
    }
    const auto pieces = path_ ? std::vector<RasterTriangle>{}
                              : RasterTriangle::prepare(grid.camera, scene_.instances(), grid.depth, stop);
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
