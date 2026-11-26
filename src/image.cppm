export module formlight.image;

import std;

export namespace formlight
{
struct ImageExtent
{
    int width, height;
};

// Integer image coordinates, measured right and down from the top-left pixel.
struct PixelCoord
{
    int x, y;
};

struct LinearColor
{
    float r{}, g{}, b{}, a{1};
};

[[nodiscard]] constexpr LinearColor scale_rgb(LinearColor color, double weight) noexcept
{
    return {static_cast<float>(color.r * weight), static_cast<float>(color.g * weight),
            static_cast<float>(color.b * weight), color.a};
}

[[nodiscard]] inline float srgb_to_linear(float value) noexcept
{
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] inline float linear_to_srgb(float value) noexcept
{
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.0031308f ? 12.92f * value : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

class ColorImage
{
  public:
    explicit ColorImage(ImageExtent extent, LinearColor fill = {}) : extent_(extent)
    {
        if (extent.width <= 0 || extent.height <= 0 ||
            static_cast<std::size_t>(extent.width) > pixels_.max_size() / static_cast<std::size_t>(extent.height))
        {
            throw std::invalid_argument("Invalid image dimensions");
        }
        pixels_.assign(static_cast<std::size_t>(extent.width) * extent.height, fill);
    }

    [[nodiscard]] ImageExtent extent() const noexcept
    {
        return extent_;
    }
    [[nodiscard]] LinearColor &operator[](int y, int x) noexcept
    {
        return pixels_[offset(y, x)];
    }
    [[nodiscard]] const LinearColor &operator[](int y, int x) const noexcept
    {
        return pixels_[offset(y, x)];
    }

  private:
    [[nodiscard]] std::size_t offset(int y, int x) const noexcept
    {
        return static_cast<std::size_t>(y) * extent_.width + x;
    }
    ImageExtent extent_;
    std::vector<LinearColor> pixels_;
};
} // namespace formlight
