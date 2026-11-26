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

enum class ToneMapping
{
    Clip,
    Reinhard
};

struct DisplaySettings
{
    static constexpr double min_exposure_ev = -16, max_exposure_ev = 16;
    double exposure_ev{};
    ToneMapping tone_mapping{ToneMapping::Clip};

    void fields(this auto &self, auto &&field)
    {
        field("exposure_ev", self.exposure_ev);
        field("tone_mapping", self.tone_mapping);
    }

    void validate() const
    {
        if (!(exposure_ev >= min_exposure_ev && exposure_ev <= max_exposure_ev))
        {
            throw std::invalid_argument(
                std::format("display.exposure_ev must be between {} and {}", min_exposure_ev, max_exposure_ev));
        }
    }
};

// Prepare the linear exposure multiplier once per image, before sRGB encoding.
class DisplayTransform
{
  public:
    explicit DisplayTransform(const DisplaySettings &settings) : tone_mapping_(settings.tone_mapping)
    {
        settings.validate();
        exposure_scale_ = std::exp2(settings.exposure_ev);
    }

    [[nodiscard]] std::array<std::uint8_t, 3> rgb8(LinearColor color) const noexcept
    {
        return {encode(color.r), encode(color.g), encode(color.b)};
    }

  private:
    [[nodiscard]] std::uint8_t encode(float channel) const noexcept
    {
        const double exposed = std::max(0.0, channel * exposure_scale_);
        // Per-channel Reinhard: x / (1 + x). This form also maps positive infinity to white.
        const double mapped =
            tone_mapping_ == ToneMapping::Reinhard ? 1.0 - 1.0 / (1.0 + exposed) : std::min(1.0, exposed);
        return static_cast<std::uint8_t>(std::lround(linear_to_srgb(static_cast<float>(mapped)) * 255.0f));
    }

    ToneMapping tone_mapping_;
    double exposure_scale_;
};

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
