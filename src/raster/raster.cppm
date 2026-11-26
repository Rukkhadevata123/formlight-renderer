export module formlight.raster;

import std;
import formlight.raster_coverage;
export import formlight.camera;
export import formlight.mesh_instance;

namespace formlight
{
export struct RasterHit
{
    TrianglePoint surface;
    double view_depth;

    void keep_nearest(std::optional<RasterHit> &pixel) const noexcept
    {
        if (!pixel || std::tie(view_depth, surface.id) < std::tie(pixel->view_depth, pixel->surface.id))
        {
            pixel = *this;
        }
    }
};

// Row-major [row, column] view of caller-owned pixels. An empty cell is background.
export using VisibilityBuffer = std::mdspan<std::optional<RasterHit>, std::dextents<int, 2>>;

struct RasterVertex
{
    ProjectedPoint point;
    Barycentrics source_weights;
};

export class RasterTriangle
{
  public:
    // Clip to the closed view-depth range, then triangulate and project the surviving polygons.
    [[nodiscard]] static std::vector<RasterTriangle> prepare(const PinholeCamera &camera,
                                                             std::span<const MeshInstance> instances,
                                                             ViewDepthRange range, std::stop_token stop = {});
    [[nodiscard]] std::optional<RasterHit> sample(ImagePoint point) const noexcept;
    // Keep the nearest pixel-center hit. Exact depth ties use the smaller (instance, primitive) pair.
    void rasterize(VisibilityBuffer target) const noexcept;

  private:
    RasterTriangle(SurfaceId source, const RasterVertex &a, const RasterVertex &b, const RasterVertex &c);
    SurfaceId source_;
    ScreenTriangle coverage_;
    [[nodiscard]] RasterHit interpolate(const std::array<double, 3> &values) const noexcept;
    std::array<double, 3> inverse_depths_;
    std::array<Barycentrics, 3> source_weights_;
};

RasterTriangle::RasterTriangle(SurfaceId source, const RasterVertex &a, const RasterVertex &b, const RasterVertex &c)
    : source_(source), coverage_(a.point.image, b.point.image, c.point.image),
      inverse_depths_{1.0 / a.point.view_depth, 1.0 / b.point.view_depth, 1.0 / c.point.view_depth},
      source_weights_{a.source_weights, b.source_weights, c.source_weights}
{
}

std::optional<RasterHit> RasterTriangle::sample(ImagePoint point) const noexcept
{
    const auto values = coverage_.edge_values(point);
    if (!values)
    {
        return std::nullopt;
    }
    return interpolate(*values);
}

RasterHit RasterTriangle::interpolate(const std::array<double, 3> &values) const noexcept
{
    const auto [a, b, c] = values;
    // Edge values are screen barycentrics scaled by twice_area_. Divide each by its vertex depth
    // before normalizing to recover barycentrics on the world-space triangle.
    const double wa = a * inverse_depths_[0];
    const double wb = b * inverse_depths_[1];
    const double wc = c * inverse_depths_[2];
    const double sum = wa + wb + wc;
    // The clipped vertices carry barycentrics in the original primitive.
    const auto &[v0, v1, v2] = source_weights_;
    const Barycentrics weights{
        (wa * v0.a + wb * v1.a + wc * v2.a) / sum,
        (wa * v0.b + wb * v1.b + wc * v2.b) / sum,
        (wa * v0.c + wb * v1.c + wc * v2.c) / sum,
    };
    return RasterHit{.surface = {source_, weights}, .view_depth = coverage_.twice_area() / sum};
}

void RasterTriangle::rasterize(VisibilityBuffer target) const noexcept
{
    coverage_.for_each_sample({target.extent(1), target.extent(0)}, [&](int x, int y, const auto &values)
                              { interpolate(values).keep_nearest(target[y, x]); });
}
} // namespace formlight
