export module formlight.image_codec;

import std;
export import formlight.image;

export namespace formlight
{
[[nodiscard]] ColorImage decode_base_color(std::span<const std::byte> encoded);
void write_png(const std::filesystem::path &path, const ColorImage &image);
} // namespace formlight
