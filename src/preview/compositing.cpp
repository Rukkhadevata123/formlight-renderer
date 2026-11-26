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

struct BlendFragment
{
    int column;
    RasterHit hit;
    bool back_face;
};
using BlendRows = std::vector<std::vector<BlendFragment>>;

enum class SurfacePass
{
    Solid,
    BlendFront,
    BlendBack,
    Cull
};

SurfacePass surface_pass(const GltfScene &scene, SurfaceId id, Vec3 eye)
{
    const auto &instance = scene.instance(id.instance);
    const auto &face = instance.triangles[std::to_underlying(id.primitive)];
    const auto &material = scene.material(face);
    if (material.alpha_mode != AlphaMode::Blend)
    {
        return SurfacePass::Solid;
    }
    // Local winding still identifies the authored front after a mirrored instance transform.
    const auto to_eye = instance.transform.to_local_point(eye) - face.vertices[0].position;
    if (dot(face.geometric_normal(), to_eye) >= 0)
    {
        return SurfacePass::BlendFront;
    }
    return material.double_sided ? SurfacePass::BlendBack : SurfacePass::Cull;
}

std::uint64_t pixel_index(PixelCoord pixel, ImageExtent extent) noexcept
{
    return static_cast<std::uint64_t>(pixel.y) * extent.width + pixel.x;
}

bool in_front_of_solid(const RasterHit &hit, const std::optional<RasterHit> &solid)
{
    return !solid || hit.view_depth < solid->view_depth;
}

BlendRows rasterize_visibility(const GltfScene &scene, const PreviewView &view, const SurfaceFilter &solid_filter,
                               VisibilityBuffer visible, std::stop_token stop)
{
    const auto pieces = RasterTriangle::prepare(view.camera, scene.instances(), view.depth, stop);
    std::vector<std::pair<const RasterTriangle *, bool>> blended;
    for (const auto &piece : pieces)
    {
        if (stop.stop_requested())
        {
            throw std::runtime_error("Rendering cancelled");
        }
        const auto pass = surface_pass(scene, piece.source(), view.camera.view().position);
        if (pass == SurfacePass::Solid)
        {
            piece.rasterize(visible, solid_filter);
        }
        else if (pass != SurfacePass::Cull)
        {
            blended.emplace_back(&piece, pass == SurfacePass::BlendBack);
        }
    }

    if (blended.empty())
    {
        return {};
    }
    const auto extent = view.camera.extent();
    BlendRows rows(extent.height);
    for (const auto &[piece, back_face] : blended)
    {
        if (stop.stop_requested())
        {
            throw std::runtime_error("Rendering cancelled");
        }
        const auto collect = [&](int x, int y, const RasterHit &hit)
        {
            if (in_front_of_solid(hit, visible[y, x]))
            {
                rows[y].push_back({x, hit, back_face});
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
    for (const auto &[x, hit, back_face] : fragments)
    {
        const auto src = shade(x, hit, back_face);
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
    const ImageExtent extent = view.camera.extent();
    if (extent.width > std::numeric_limits<int>::max() / extent.height)
    {
        throw std::invalid_argument("Preview exceeds visibility buffer capacity");
    }
    ColorImage image{extent, background};
    std::vector<std::optional<RasterHit>> pixels(static_cast<std::size_t>(extent.width) * extent.height);
    const VisibilityBuffer visible{pixels.data(), extent.height, extent.width};
    auto blends = rasterize_visibility(scene_, view, solid_filter_, visible, stop);
    const auto shade_row = [&](int y)
    {
        const auto shade_hit = [&](int x, const RasterHit &hit, bool back_face)
        { return shade(hit, pixel_index({x, y}, extent), view.area_light_pattern, stop, back_face); };
        if (stop.stop_requested())
        {
            throw std::runtime_error("Rendering cancelled");
        }
        for (int x = 0; x < extent.width; ++x)
        {
            if (const auto &hit = visible[y, x])
            {
                image[y, x] = shade_hit(x, *hit, false);
            }
        }
        if (!blends.empty())
        {
            composite_layers({&image[y, 0], static_cast<std::size_t>(extent.width)}, blends[y], shade_hit);
        }
    };
    // Only the render's stop token may interrupt the frame; a cancelled outer TBB task could skip rows.
    oneapi::tbb::task_group_context frame_tasks{oneapi::tbb::task_group_context::isolated};
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
    const auto center = pixel_center(pixel.x, pixel.y);
    std::optional<RasterHit> solid;
    std::vector<BlendFragment> blends;
    for (const auto &piece : RasterTriangle::prepare(view.camera, scene_.instances(), view.depth, stop))
    {
        if (stop.stop_requested())
        {
            throw std::runtime_error("Pixel inspection cancelled");
        }
        if (const auto hit = piece.sample(center))
        {
            const auto pass = surface_pass(scene_, hit->surface.id, view.camera.view().position);
            if (pass == SurfacePass::Solid)
            {
                hit->keep_nearest(solid, solid_filter_);
            }
            else if (pass != SurfacePass::Cull)
            {
                blends.push_back({0, *hit, pass == SurfacePass::BlendBack});
            }
        }
    }

    PixelDiagnostic report{.pixel = pixel, .color = background};
    const auto shade_hit = [&](int, const RasterHit &hit, bool back_face)
    { return shade(hit, pixel_index(pixel, extent), view.area_light_pattern, stop, back_face, &report); };
    if (solid)
    {
        report.color = shade_hit(0, *solid, false);
    }
    std::erase_if(blends, [&](const auto &fragment) { return !in_front_of_solid(fragment.hit, solid); });
    composite_layers({&report.color, 1}, blends, shade_hit);
    return report;
}
} // namespace formlight
