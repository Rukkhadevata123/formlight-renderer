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

namespace formlight
{
ColorImage decode_base_color(std::span<const std::byte> encoded)
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
        throw std::runtime_error(std::format("Cannot decode base-color image: {}", stbi_failure_reason()));
    }
    static const auto linear = []
    {
        std::array<float, 256> values{};
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            values[i] = srgb_to_linear(static_cast<float>(i) / 255.0f);
        }
        return values;
    }();
    ColorImage image{{width, height}};
    const auto *pixel = bytes.get();
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x, pixel += 4)
        {
            image[y, x] = {linear[pixel[0]], linear[pixel[1]], linear[pixel[2]], static_cast<float>(pixel[3]) / 255.0f};
        }
    }
    return image;
}

void write_png(const std::filesystem::path &path, const ColorImage &image)
{
    const auto [width, height] = image.extent();
    if (width > (std::numeric_limits<int>::max() / height - 1) / 3)
    {
        throw std::runtime_error("Image is too large for the PNG encoder");
    }
    std::vector<unsigned char> rgb(static_cast<std::size_t>(width) * height * 3);
    auto out = rgb.begin();
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto color = image[y, x];
            for (float value : {color.r, color.g, color.b})
            {
                *out++ = static_cast<unsigned char>(std::lround(linear_to_srgb(value) * 255.0f));
            }
        }
    }
    auto temporary = path;
    temporary += std::format(".{:08x}.tmp", std::random_device{}());
    std::ofstream file(temporary, std::ios::binary | std::ios::noreplace);
    if (!file)
    {
        throw std::runtime_error(std::format("Cannot open PNG output: {}", path.string()));
    }
    const auto write = [](void *stream, void *data, int size)
    { static_cast<std::ostream *>(stream)->write(static_cast<const char *>(data), size); };
    try
    {
        const int encoded =
            stbi_write_png_to_func(write, static_cast<std::ostream *>(&file), width, height, 3, rgb.data(), 3 * width);
        file.close();
        if (!encoded || !file)
        {
            throw std::runtime_error(std::format("Cannot write PNG: {}", path.string()));
        }
        std::filesystem::rename(temporary, path);
    }
    catch (...)
    {
        file.close();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}
} // namespace formlight
