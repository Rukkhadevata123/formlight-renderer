export module formlight.texture;

import std;
export import formlight.image;
export import formlight.triangle;

namespace formlight
{
export enum class WrapMode
{
    Repeat,
    ClampToEdge,
    MirroredRepeat
};
export enum class TexelFilter
{
    Nearest,
    Linear
};
export enum class MipFilter
{
    None,
    Nearest,
    Linear
};
export struct TextureSampler
{
    WrapMode u = WrapMode::Repeat, v = WrapMode::Repeat;
    TexelFilter mag = TexelFilter::Linear, min = TexelFilter::Linear;
    MipFilter mip = MipFilter::None;
};

namespace
{
double address(double uv, WrapMode mode)
{
    if (mode == WrapMode::ClampToEdge)
    {
        return std::clamp(uv, 0.0, 1.0);
    }
    if (mode == WrapMode::MirroredRepeat)
    {
        double phase = std::fmod(uv, 2.0);
        if (phase < 0)
        {
            phase += 2.0;
        }
        return phase <= 1.0 ? phase : 2.0 - phase;
    }
    return uv - std::floor(uv);
}

int texel(int index, int size, WrapMode mode)
{
    if (mode == WrapMode::Repeat)
    {
        if (index < 0)
        {
            return size - 1;
        }
        if (index >= size)
        {
            return 0;
        }
    }
    return std::clamp(index, 0, size - 1);
}

LinearColor mix(LinearColor a, LinearColor b, float t)
{
    return {std::lerp(a.r, b.r, t), std::lerp(a.g, b.g, t), std::lerp(a.b, b.b, t), std::lerp(a.a, b.a, t)};
}
// glTF's (0,0) addresses the top-left of each mip image.
LinearColor sample_level(const ColorImage &image, TexCoord uv, TextureSampler sampler, TexelFilter filter)
{
    const auto [width, height] = image.extent();
    const double x = address(uv.u, sampler.u) * width;
    const double y = address(uv.v, sampler.v) * height;
    if (filter == TexelFilter::Nearest)
    {
        return image[texel(static_cast<int>(y), height, sampler.v), texel(static_cast<int>(x), width, sampler.u)];
    }

    const double left = std::floor(x - 0.5), top = std::floor(y - 0.5);
    const int x0 = texel(static_cast<int>(left), width, sampler.u),
              x1 = texel(static_cast<int>(left) + 1, width, sampler.u);
    const int y0 = texel(static_cast<int>(top), height, sampler.v),
              y1 = texel(static_cast<int>(top) + 1, height, sampler.v);
    const float fx = static_cast<float>(x - 0.5 - left), fy = static_cast<float>(y - 0.5 - top);
    return mix(mix(image[y0, x0], image[y0, x1], fx), mix(image[y1, x0], image[y1, x1], fx), fy);
}

ColorImage downsample(const ColorImage &source)
{
    const auto [w, h] = source.extent();
    const ImageExtent extent{std::max(1, w / 2), std::max(1, h / 2)};
    ColorImage result{extent};
    // A destination texel covers an exact box in the source, including fractional edges for odd sizes.
    for (int y = 0; y < extent.height; ++y)
    {
        const double top = static_cast<double>(y) * h / extent.height;
        const double bottom = static_cast<double>(y + 1) * h / extent.height;
        for (int x = 0; x < extent.width; ++x)
        {
            const double left = static_cast<double>(x) * w / extent.width;
            const double right = static_cast<double>(x + 1) * w / extent.width;
            LinearColor mean{0, 0, 0, 0};
            const double area = (right - left) * (bottom - top);
            for (int sy = static_cast<int>(top); sy < bottom; ++sy)
            {
                const double dy = std::min(bottom, sy + 1.0) - std::max(top, static_cast<double>(sy));
                for (int sx = static_cast<int>(left); sx < right; ++sx)
                {
                    const double dx = std::min(right, sx + 1.0) - std::max(left, static_cast<double>(sx));
                    const auto weight = static_cast<float>(dx * dy / area);
                    const auto color = source[sy, sx];
                    mean.r += weight * color.r;
                    mean.g += weight * color.g;
                    mean.b += weight * color.b;
                    mean.a += weight * color.a;
                }
            }
            result[y, x] = mean;
        }
    }
    return result;
}
} // namespace

export class TextureImage
{
  public:
    explicit TextureImage(ColorImage base)
    {
        levels_.push_back(std::move(base));
    }

    void generate_mipmaps()
    {
        auto extent = levels_.back().extent();
        while (extent.width > 1 || extent.height > 1)
        {
            levels_.push_back(downsample(levels_.back()));
            extent = levels_.back().extent();
        }
    }

    [[nodiscard]] LinearColor sample(TexCoord uv, TextureSampler sampler, UvGradients gradients = {}) const
    {
        const auto &base = levels_.front();
        if (sampler.mag == sampler.min && sampler.mip == MipFilter::None)
        {
            return sample_level(base, uv, sampler, sampler.mag);
        }
        const auto [w, h] = base.extent();
        const auto texel_span_squared = [=](TexCoord d)
        {
            const double u = d.u * w, v = d.v * h;
            return u * u + v * v;
        };
        const double rho2 = std::max(texel_span_squared(gradients.dx), texel_span_squared(gradients.dy));
        if (!(rho2 > 1))
        {
            return sample_level(base, uv, sampler, sampler.mag);
        }
        if (sampler.mip == MipFilter::None)
        {
            return sample_level(base, uv, sampler, sampler.min);
        }
        const auto last = levels_.size() - 1;
        const double lod = std::min(0.5 * std::log2(rho2), static_cast<double>(last));
        if (sampler.mip == MipFilter::Nearest)
        {
            return sample_level(levels_[static_cast<std::size_t>(std::round(lod))], uv, sampler, sampler.min);
        }
        const double lower_lod = std::floor(lod);
        const auto lower = static_cast<std::size_t>(lower_lod);
        const auto first = sample_level(levels_[lower], uv, sampler, sampler.min);
        if (lod == lower_lod)
        {
            return first;
        }
        return mix(first, sample_level(levels_[lower + 1], uv, sampler, sampler.min),
                   static_cast<float>(lod - lower_lod));
    }

  private:
    std::vector<ColorImage> levels_;
};
} // namespace formlight
