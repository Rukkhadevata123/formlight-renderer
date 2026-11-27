module;

#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/task_group.h>

module formlight.preview;

import std;

namespace formlight
{
namespace
{
constexpr LinearColor background{0.035f, 0.045f, 0.06f, 1.0f};

PreviewView sample_grid_view(const PreviewView &view)
{
    const auto extent = sample_grid_extent(view.camera.extent(), view.ssaa);
    if (view.ssaa == 1)
    {
        return view;
    }
    return {PinholeCamera{view.camera.view(), extent}, view.depth, view.area_light_pattern};
}

LinearColor resolve_samples(int count, auto color_at)
{
    Vec3 sum{};
    for (int i = 0; i < count; ++i)
    {
        const auto c = color_at(i);
        sum = sum + Vec3{c.r, c.g, c.b};
    }
    // All samples have already been composited onto the opaque background.
    return {static_cast<float>(sum.x / count), static_cast<float>(sum.y / count), static_cast<float>(sum.z / count), 1};
}

struct BlendFragment
{
    int column;
    RasterHit hit;
};
using BlendRows = std::vector<std::vector<BlendFragment>>;

enum class SurfacePass
{
    Solid,
    Blend,
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
    return material.alpha_mode == AlphaMode::Blend ? SurfacePass::Blend : SurfacePass::Solid;
}

bool in_front_of_solid(const RasterHit &hit, const std::optional<RasterHit> &solid)
{
    return !solid || hit.view_depth < solid->view_depth;
}

BlendRows rasterize_visibility(const GltfScene &scene, const PreviewView &view, const SurfaceFilter &solid_filter,
                               VisibilityBuffer visible, std::stop_token stop)
{
    const auto pieces = RasterTriangle::prepare(view.camera, scene.instances(), view.depth, stop);
    std::vector<const RasterTriangle *> blended;
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
        else if (pass == SurfacePass::Blend)
        {
            blended.push_back(&piece);
        }
    }

    if (blended.empty())
    {
        return {};
    }
    const auto extent = view.camera.extent();
    BlendRows rows(extent.height);
    for (const auto *piece : blended)
    {
        if (stop.stop_requested())
        {
            throw std::runtime_error("Rendering cancelled");
        }
        const auto collect = [&](int x, int y, const RasterHit &hit)
        {
            if (in_front_of_solid(hit, visible[y, x]))
            {
                rows[y].push_back({x, hit});
            }
        };
        piece->for_each_hit(extent, collect);
    }
    return rows;
}

void composite_layers(std::span<LinearColor> row, std::span<BlendFragment> fragments, auto shade)
{
    // Sort each pixel back to front. At equal depth the smaller surface ID is on top.
    const auto back_to_front = [](const BlendFragment &a, const BlendFragment &b)
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
        const auto src = shade(x, hit);
        if (src.a > 0)
        {
            auto &dst = row[x];
            // The background and depth-writing surfaces are opaque, so the composite alpha stays one.
            dst = {std::lerp(dst.r, src.r, src.a), std::lerp(dst.g, src.g, src.a), std::lerp(dst.b, src.b, src.a), 1};
        }
    }
}
} // namespace

ColorImage PreviewRenderer::render(const PreviewView &view, std::stop_token stop) const
{
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
    if (path_)
    {
        ColorImage image{extent};
        const auto trace_row = [&](int y)
        {
            if (stop.stop_requested())
            {
                throw std::runtime_error("Rendering cancelled");
            }
            for (int x = 0; x < extent.width; ++x)
            {
                image[y, x] = trace_primary(view, {x, y});
            }
        };
        oneapi::tbb::parallel_for(0, extent.height, trace_row, frame_tasks);
        return image;
    }
    ColorImage image{extent, background};
    std::vector<std::optional<RasterHit>> pixels(static_cast<std::size_t>(extent.width) * extent.height);
    const VisibilityBuffer visible{pixels.data(), extent.height, extent.width};
    auto blends = rasterize_visibility(scene_, view, solid_filter_, visible, stop);
    const auto shade_row = [&](int y)
    {
        const auto shade_hit = [&](int x, const RasterHit &hit) { return shade(hit, {x, y}, view, stop); };
        if (stop.stop_requested())
        {
            throw std::runtime_error("Rendering cancelled");
        }
        for (int x = 0; x < extent.width; ++x)
        {
            if (const auto &hit = visible[y, x])
            {
                image[y, x] = shade_hit(x, *hit);
            }
        }
        if (!blends.empty())
        {
            composite_layers({&image[y, 0], static_cast<std::size_t>(extent.width)}, blends[y], shade_hit);
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
    for (int i = 0; i < count; ++i)
    {
        const PixelCoord sample{pixel.x * axis + i % axis, pixel.y * axis + i / axis};
        const auto center = pixel_center(sample.x, sample.y);
        auto &record = report.samples.emplace_back(SampleDiagnostic{
            .position = {center.x / axis, center.y / axis}, .weight = 1.0 / count, .color = background});
        if (path_)
        {
            if (stop.stop_requested())
            {
                throw std::runtime_error("Pixel inspection cancelled");
            }
            record.color = trace_primary(grid, sample, &record);
        }
        else
        {
            diagnose_sample(grid, sample, pieces, record, stop);
        }
    }
    report.color = resolve_samples(count, [&](int i) { return report.samples[i].color; });
    return report;
}

LinearColor PreviewRenderer::trace_primary(const PreviewView &view, PixelCoord pixel, SampleDiagnostic *record) const
{
    const auto ray = view.camera.ray_for_sample(pixel_center(pixel.x, pixel.y));
    const double depth_per_unit = view.camera.forward_component(ray.direction);
    // The camera depth range includes both endpoints; ray queries have open intervals.
    const RayInterval interval{
        std::nextafter(view.depth.near_depth / depth_per_unit, 0.0),
        std::nextafter(view.depth.far_depth / depth_per_unit, std::numeric_limits<double>::infinity())};
    std::optional<PathHit> first;
    const auto color = path_->radiance(ray, interval, record ? &first : nullptr);
    if (record && first)
    {
        const auto &hit = first->intersection;
        const auto id = hit.surface.id;
        const auto &instance = scene_.instance(id.instance);
        const auto &face = instance.triangles[std::to_underlying(id.primitive)];
        const bool back_face = dot(face.geometric_normal(), instance.transform.to_local_vector(ray.direction)) > 0;
        record->surface = DiagnosticSurface{
            {hit.surface, hit.t * depth_per_unit, back_face}, face.material_index, first->material, {}};
    }
    return color;
}

void PreviewRenderer::diagnose_sample(const PreviewView &view, PixelCoord pixel, std::span<const RasterTriangle> pieces,
                                      SampleDiagnostic &report, std::stop_token stop) const
{
    const auto center = pixel_center(pixel.x, pixel.y);
    std::optional<RasterHit> solid;
    std::vector<BlendFragment> blends;
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
            else if (pass == SurfacePass::Blend)
            {
                blends.push_back({0, *hit});
            }
        }
    }

    const auto shade_hit = [&](int, const RasterHit &hit) { return shade(hit, pixel, view, stop, &report); };
    if (solid)
    {
        report.color = shade_hit(0, *solid);
    }
    std::erase_if(blends, [&](const auto &fragment) { return !in_front_of_solid(fragment.hit, solid); });
    composite_layers({&report.color, 1}, blends, shade_hit);
}
} // namespace formlight
