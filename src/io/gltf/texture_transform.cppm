module;
#include <fastgltf/types.hpp>

export module formlight.gltf_texture_transform;

import std;
import formlight.gltf;

namespace formlight::gltf_detail
{
export std::size_t texcoord_set(const fastgltf::TextureInfo &info)
{
    return info.transform ? info.transform->texCoordIndex.value_or(info.texCoordIndex) : info.texCoordIndex;
}

export std::optional<UvTransform> read_uv_transform(const fastgltf::TextureInfo &info)
{
    if (!info.transform)
    {
        return {};
    }
    const auto &t = *info.transform;
    const double angle = t.rotation, su = t.uvScale[0], sv = t.uvScale[1];
    const TexCoord offset{t.uvOffset[0], t.uvOffset[1]};
    if (!std::isfinite(angle) || !std::isfinite(su) || !std::isfinite(sv) || !std::isfinite(offset.u) ||
        !std::isfinite(offset.v))
    {
        throw std::runtime_error("KHR_texture_transform requires finite offset, rotation and scale");
    }
    const auto c = std::cos(angle), s = std::sin(angle);
    return UvTransform{.u_axis = {c * su, s * su}, .v_axis = {-s * sv, c * sv}, .offset = offset};
}
} // namespace formlight::gltf_detail
