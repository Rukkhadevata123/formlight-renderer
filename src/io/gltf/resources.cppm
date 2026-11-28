module;

#include <fastgltf/core.hpp>

export module formlight.gltf_resources;

import std;

namespace formlight::gltf_detail
{
export using Path = std::filesystem::path;

export std::string path_text(const Path &path)
{
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

export std::vector<std::byte> read_resource_bytes(const Path &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const std::streamoff length = file.tellg();
    if (length < 0 || !std::in_range<std::size_t>(length))
    {
        throw std::runtime_error(std::format("Cannot open or size external resource: {}", path_text(path)));
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    if (!file.seekg(0) || !file.read(reinterpret_cast<char *>(bytes.data()), length))
    {
        throw std::runtime_error(std::format("Cannot read external resource: {}", path_text(path)));
    }
    return bytes;
}

export struct ModelResources
{
    fastgltf::Asset &asset;
    Path directory;
    // Byte views in the parsed asset borrow this storage until mesh and texture import finishes.
    std::map<Path, std::vector<std::byte>> files{};

    std::span<const std::byte> buffer_bytes(std::size_t index);
    const fastgltf::Accessor &geometry_accessor(std::size_t index);

    Path local_path(const fastgltf::sources::URI &source) const
    {
        const auto &uri = *source.uri;
        const auto relative = uri.fspath();
        if (!uri.scheme().empty() || !uri.host().empty() || relative.empty() || relative.has_root_path() ||
            relative.native().find(Path::value_type{}) != Path::string_type::npos)
        {
            return {};
        }
        return (directory / relative).lexically_normal();
    }

    Path require_path(const fastgltf::sources::URI &source) const
    {
        auto path = local_path(source);
        if (path.empty())
        {
            throw std::runtime_error(std::format("Expected a relative file URI: {}", source.uri->string()));
        }
        return path;
    }

    std::span<const std::byte> read(const Path &path)
    {
        if (const auto it = files.find(path); it != files.end())
        {
            return it->second;
        }
        return files.emplace(path, read_resource_bytes(path)).first->second;
    }
};

export std::span<const std::byte> loaded_bytes(const fastgltf::DataSource &source)
{
    if (const auto *a = std::get_if<fastgltf::sources::Array>(&source))
    {
        return {a->bytes.data(), a->bytes.size_bytes()};
    }
    if (const auto *v = std::get_if<fastgltf::sources::Vector>(&source))
    {
        return v->bytes;
    }
    if (const auto *v = std::get_if<fastgltf::sources::ByteView>(&source))
    {
        return v->bytes;
    }
    throw std::runtime_error("Expected glTF buffer or image bytes");
}

std::span<const std::byte> ModelResources::buffer_bytes(std::size_t index)
{
    auto &buffer = asset.buffers.at(index);
    if (const auto *uri = std::get_if<fastgltf::sources::URI>(&buffer.data))
    {
        const auto path = require_path(*uri);
        const auto bytes = read(path);
        if (bytes.size() < buffer.byteLength)
        {
            throw std::runtime_error(std::format("{}: buffer declares {} bytes, file contains {}", path_text(path),
                                                 buffer.byteLength, bytes.size()));
        }
        buffer.data = fastgltf::sources::ByteView{bytes.first(buffer.byteLength), uri->mimeType};
    }
    return loaded_bytes(buffer.data);
}

const fastgltf::Accessor &ModelResources::geometry_accessor(std::size_t index)
{
    const auto &accessor = asset.accessors.at(index);
    if (accessor.sparse)
    {
        throw std::runtime_error("Sparse geometry accessors are not supported yet");
    }
    if (accessor.bufferViewIndex)
    {
        const auto &view = asset.bufferViews.at(*accessor.bufferViewIndex);
        buffer_bytes(view.bufferIndex);
    }
    return accessor;
}
} // namespace formlight::gltf_detail
