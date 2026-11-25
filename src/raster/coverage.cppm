export module formlight.raster_coverage;

import std;
import formlight.camera;

namespace formlight
{
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

// Owns the common pixel-center and top-left rules for perspective and orthographic passes.
export class ScreenTriangle
{
  public:
    ScreenTriangle(ImagePoint a, ImagePoint b, ImagePoint c)
        : edges_{EdgeEquation{b, c}, EdgeEquation{c, a}, EdgeEquation{a, b}}, twice_area_(edges_[0].at(a)),
          min_corner_{std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y})},
          max_corner_{std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y})}
    {
        if (twice_area_ < 0)
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

    [[nodiscard]] double twice_area() const noexcept
    {
        return twice_area_;
    }

    // A covered sample's edge values are its screen barycentrics multiplied by twice_area().
    [[nodiscard]] std::optional<std::array<double, 3>> edge_values(ImagePoint point) const noexcept
    {
        const std::array values{edges_[0].at(point), edges_[1].at(point), edges_[2].at(point)};
        if (twice_area_ == 0 || !edges_[0].covers(values[0]) || !edges_[1].covers(values[1]) ||
            !edges_[2].covers(values[2]))
        {
            return std::nullopt;
        }
        return values;
    }

    void for_each_sample(ImageExtent extent, auto &&visit) const
    {
        // Clamp before converting off-screen coordinates to integer indices.
        const auto bound = [](double position, int extent)
        { return static_cast<int>(std::clamp(position, 0.0, static_cast<double>(extent))); };
        const int left = bound(std::floor(min_corner_.x), extent.width);
        const int right = bound(std::ceil(max_corner_.x), extent.width);
        const int top = bound(std::floor(min_corner_.y), extent.height);
        const int bottom = bound(std::ceil(max_corner_.y), extent.height);
        for (int row = top; row < bottom; ++row)
        {
            for (int column = left; column < right; ++column)
            {
                if (const auto values = edge_values(pixel_center(column, row)))
                {
                    visit(column, row, *values);
                }
            }
        }
    }

  private:
    std::array<EdgeEquation, 3> edges_;
    double twice_area_;
    ImagePoint min_corner_, max_corner_;
};
} // namespace formlight
