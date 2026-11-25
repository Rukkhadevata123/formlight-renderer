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
export struct TextureSampler
{
    WrapMode u = WrapMode::Repeat, v = WrapMode::Repeat;
    TexelFilter filter = TexelFilter::Linear;
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
} // namespace

// Sample mip level zero in linear color. glTF's (0,0) addresses the top-left of the decoded image.
export [[nodiscard]] LinearColor sample_texture(const ColorImage &image, TexCoord uv, TextureSampler sampler)
{
    const auto [width, height] = image.extent();
    const double x = address(uv.u, sampler.u) * width;
    const double y = address(uv.v, sampler.v) * height;
    if (sampler.filter == TexelFilter::Nearest)
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
} // namespace formlight
