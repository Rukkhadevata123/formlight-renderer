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

// Prepare once per triangle from finite, projected vertices with positive view depths.
export class RasterTriangle
{
  public:
    RasterTriangle(PrimitiveId primitive, const std::array<ProjectedPoint, 3> &vertices);
    [[nodiscard]] std::optional<RasterHit> sample(ImagePoint point) const noexcept;

  private:
    PrimitiveId primitive_;
    std::array<EdgeEquation, 3> edges_;
    std::array<double, 3> inverse_depths_;
    double twice_area_;
};

RasterTriangle::RasterTriangle(PrimitiveId primitive, const std::array<ProjectedPoint, 3> &vertices)
    : primitive_(primitive),
      edges_{EdgeEquation{vertices[1].image, vertices[2].image}, EdgeEquation{vertices[2].image, vertices[0].image},
             EdgeEquation{vertices[0].image, vertices[1].image}},
      inverse_depths_{1.0 / vertices[0].view_depth, 1.0 / vertices[1].view_depth, 1.0 / vertices[2].view_depth},
      twice_area_(edges_[0].at(vertices[0].image))
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
    return RasterHit{.surface = {primitive_, {wa / sum, wb / sum, wc / sum}}, .view_depth = twice_area_ / sum};
}
} // namespace formlight
