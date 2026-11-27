export module formlight.image_codec;

import std;
export import formlight.image;

export namespace formlight
{
enum class RgbEncoding
{
    Srgb,
    Linear
};

[[nodiscard]] ColorImage decode_image(std::span<const std::byte> encoded, RgbEncoding encoding);
void write_png(const std::filesystem::path &path, const ColorImage &image, const DisplaySettings &display = {});
} // namespace formlight
