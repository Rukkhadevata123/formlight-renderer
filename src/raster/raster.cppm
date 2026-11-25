export module formlight.raster;

import std;
export import formlight.camera;
export import formlight.triangle;

namespace formlight
{
export struct RasterHit
{
    TrianglePoint surface;
    double view_depth;
};

struct EdgeEquation
{
    double x, y;
    bool top_left;
    ImagePoint origin;

    EdgeEquation(ImagePoint from, ImagePoint to)
        : x(from.y - to.y), y(to.x - from.x), top_left(to.y < from.y || (to.y == from.y && to.x > from.x)),
          origin(top_left ? from : to)
    {
    }

    [[nodiscard]] double at(ImagePoint sample) const noexcept
    {
        // Both windings use the same endpoint for subtraction on a shared edge.
        return x * (sample.x - origin.x) + y * (sample.y - origin.y);
    }

    [[nodiscard]] bool covers(double value) const noexcept
    {
        return value > 0.0 || (value == 0.0 && top_left);
    }
};

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
                                                             std::span<const Triangle> triangles, ViewDepthRange range);
    [[nodiscard]] std::optional<RasterHit> sample(ImagePoint point) const noexcept;

  private:
    RasterTriangle(PrimitiveId primitive, const RasterVertex &a, const RasterVertex &b, const RasterVertex &c);
    PrimitiveId primitive_;
    std::array<EdgeEquation, 3> edges_;
    std::array<double, 3> inverse_depths_;
    std::array<Barycentrics, 3> source_weights_;
    double twice_area_;
};

RasterTriangle::RasterTriangle(PrimitiveId primitive, const RasterVertex &a, const RasterVertex &b,
                               const RasterVertex &c)
    : primitive_(primitive),
      edges_{EdgeEquation{b.point.image, c.point.image}, EdgeEquation{c.point.image, a.point.image},
             EdgeEquation{a.point.image, b.point.image}},
      inverse_depths_{1.0 / a.point.view_depth, 1.0 / b.point.view_depth, 1.0 / c.point.view_depth},
      source_weights_{a.source_weights, b.source_weights, c.source_weights}, twice_area_(edges_[0].at(a.point.image))
{
    // Orient the edges inward for either winding; their indices still match the input vertices.
    if (twice_area_ < 0.0)
    {
        for (auto &edge : edges_)
        {
            edge.x = -edge.x;
            edge.y = -edge.y;
            edge.top_left = !edge.top_left;
        }
        twice_area_ = -twice_area_;
    }
}

std::optional<RasterHit> RasterTriangle::sample(ImagePoint point) const noexcept
{
    const double a = edges_[0].at(point);
    const double b = edges_[1].at(point);
    const double c = edges_[2].at(point);
    if (twice_area_ == 0.0 || !edges_[0].covers(a) || !edges_[1].covers(b) || !edges_[2].covers(c))
    {
        return std::nullopt;
    }

    // Edge values are screen barycentrics scaled by twice_area_. Divide each by its vertex depth
    // before normalizing to recover barycentrics on the world-space triangle.
    const double wa = a * inverse_depths_[0];
    const double wb = b * inverse_depths_[1];
    const double wc = c * inverse_depths_[2];
    const double sum = wa + wb + wc;
    // The clipped vertices carry barycentrics in the original primitive.
    const auto &[v0, v1, v2] = source_weights_;
    const Barycentrics source{
        (wa * v0.a + wb * v1.a + wc * v2.a) / sum,
        (wa * v0.b + wb * v1.b + wc * v2.b) / sum,
        (wa * v0.c + wb * v1.c + wc * v2.c) / sum,
    };
    return RasterHit{.surface = {primitive_, source}, .view_depth = twice_area_ / sum};
}
} // namespace formlight
