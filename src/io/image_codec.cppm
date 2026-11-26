export module formlight.image_codec;

import std;
export import formlight.image;

export namespace formlight
{
[[nodiscard]] ColorImage decode_srgb_image(std::span<const std::byte> encoded);
void write_png(const std::filesystem::path &path, const ColorImage &image, const DisplaySettings &display = {});
} // namespace formlight
