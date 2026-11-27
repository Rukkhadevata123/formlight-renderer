module;

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

module formlight.image_codec;
import std;
import formlight.output_file;

namespace formlight
{
ColorImage decode_image(std::span<const std::byte> encoded, RgbEncoding encoding)
{
    if (encoded.empty() || !std::in_range<int>(encoded.size()))
    {
        throw std::runtime_error("Invalid encoded image size");
    }
    int width{}, height{}, channels{};
    std::unique_ptr<stbi_uc, void (*)(void *)> bytes{
        stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(encoded.data()), static_cast<int>(encoded.size()),
                              &width, &height, &channels, STBI_rgb_alpha),
        &stbi_image_free};
    if (!bytes)
    {
        throw std::runtime_error(std::format("Cannot decode image: {}", stbi_failure_reason()));
    }
    static const auto normalized = []
    {
        std::array<float, 256> values{};
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            values[i] = static_cast<float>(i) / 255.0f;
        }
        return values;
    }();
    static const auto srgb = []
    {
        auto values = normalized;
        std::ranges::transform(values, values.begin(), srgb_to_linear);
        return values;
    }();
    const auto &rgb = encoding == RgbEncoding::Srgb ? srgb : normalized;
    ColorImage image{{width, height}};
    const auto *pixel = bytes.get();
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x, pixel += 4)
        {
            image[y, x] = {rgb[pixel[0]], rgb[pixel[1]], rgb[pixel[2]], normalized[pixel[3]]};
        }
    }
    return image;
}

void write_png(const std::filesystem::path &path, const ColorImage &image, const DisplaySettings &display)
{
    const auto [width, height] = image.extent();
    if (width > (std::numeric_limits<int>::max() / height - 1) / 3)
    {
        throw std::runtime_error("Image is too large for the PNG encoder");
    }
    const DisplayTransform transform{display};
    std::vector<unsigned char> rgb(static_cast<std::size_t>(width) * height * 3);
    auto out = rgb.begin();
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            out = std::ranges::copy(transform.rgb8(image[y, x]), out).out;
        }
    }
    const auto write = [](void *stream, void *data, int size)
    { static_cast<std::ostream *>(stream)->write(static_cast<const char *>(data), size); };
    replace_output_file(path,
                        [&](std::ostream &file)
                        {
                            if (!stbi_write_png_to_func(write, &file, width, height, 3, rgb.data(), 3 * width))
                            {
                                throw std::runtime_error(std::format("Cannot write PNG: {}", path.string()));
                            }
                        });
}
} // namespace formlight
