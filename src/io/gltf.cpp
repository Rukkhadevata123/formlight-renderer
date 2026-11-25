module;

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

module formlight.gltf;

import std;

namespace formlight
{
namespace
{
void check(fastgltf::Error error)
{
    if (error != fastgltf::Error::None)
    {
        throw std::runtime_error(std::string{fastgltf::getErrorMessage(error)});
    }
}

const fastgltf::Accessor &geometry_accessor(const fastgltf::Asset &asset, std::size_t index)
{
    const auto &accessor = asset.accessors.at(index);
    if (accessor.sparse)
    {
        throw std::runtime_error("Sparse geometry accessors are not supported yet");
    }
    if (accessor.bufferViewIndex)
    {
        const auto &view = asset.bufferViews.at(*accessor.bufferViewIndex);
        const auto &data = asset.buffers.at(view.bufferIndex).data;
        if (!std::holds_alternative<fastgltf::sources::Array>(data) &&
            !std::holds_alternative<fastgltf::sources::ByteView>(data) &&
            !std::holds_alternative<fastgltf::sources::Vector>(data))
        {
            throw std::runtime_error("Geometry requires an embedded buffer");
        }
    }
    return accessor;
}

std::vector<Triangle> read_mesh(const fastgltf::Asset &asset, const fastgltf::Mesh &mesh)
{
    std::vector<Triangle> triangles;
    for (const auto &primitive : mesh.primitives)
    {
        if (primitive.type != fastgltf::PrimitiveType::Triangles || !primitive.targets.empty())
        {
            throw std::runtime_error("Expected a static TRIANGLES primitive");
        }
        const auto position = primitive.findAttribute("POSITION");
        if (position == primitive.attributes.end())
        {
            throw std::runtime_error("Triangle primitive is missing POSITION");
        }
        const auto &positions = geometry_accessor(asset, position->accessorIndex);
        std::vector<TriangleVertex> vertices(positions.count);
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
            asset, positions,
            [&](auto p, std::size_t i)
            {
                if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]))
                {
                    throw std::runtime_error("POSITION contains a non-finite coordinate");
                }
                vertices[i].position = {p[0], p[1], p[2]};
            });
        const auto texcoord = primitive.findAttribute("TEXCOORD_0");
        if (texcoord != primitive.attributes.end())
        {
            const auto &uvs = geometry_accessor(asset, texcoord->accessorIndex);
            if (uvs.count != positions.count)
            {
                throw std::runtime_error("TEXCOORD_0 and POSITION have different vertex counts");
            }
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
                asset, uvs,
                [&](auto uv, std::size_t i)
                {
                    if (!std::isfinite(uv[0]) || !std::isfinite(uv[1]))
                    {
                        throw std::runtime_error("TEXCOORD_0 contains a non-finite coordinate");
                    }
                    vertices[i].uv = {uv[0], uv[1]};
                });
        }

        // GenerateMeshIndices also supplies consecutive indices for non-indexed primitives.
        const auto &indices = geometry_accessor(asset, primitive.indicesAccessor.value());
        using enum fastgltf::ComponentType;
        if (indices.type != fastgltf::AccessorType::Scalar || indices.normalized ||
            (indices.componentType != UnsignedByte && indices.componentType != UnsignedShort &&
             indices.componentType != UnsignedInt) ||
            indices.count % 3 != 0)
        {
            throw std::runtime_error("Expected unsigned triangle index triples");
        }
        if (indices.count / 3 > std::numeric_limits<std::uint32_t>::max() - triangles.size())
        {
            throw std::runtime_error("Too many triangles for mesh-local primitive IDs");
        }
        const auto start = triangles.size();
        triangles.resize(start + indices.count / 3);
        fastgltf::iterateAccessorWithIndex<std::uint32_t>(
            asset, indices,
            [&](std::uint32_t index, std::size_t i)
            {
                if (index >= vertices.size())
                {
                    throw std::runtime_error("Triangle index exceeds POSITION count");
                }
                const auto face = start + i / 3;
                auto &triangle = triangles[face];
                triangle.id = PrimitiveId{static_cast<std::uint32_t>(face)};
                triangle.vertices[i % 3] = vertices[index];
            });
    }
    return triangles;
}

AffineTransform instance_transform(const fastgltf::math::nmat4x4 &world)
{
    const auto column = [&](std::size_t i) -> Vec3 { return {world[i][0], world[i][1], world[i][2]}; };
    return {Basis3{column(0), column(1), column(2)}, column(3)};
}
} // namespace

std::expected<GltfGeometry, std::string> GltfGeometry::load_glb(const std::filesystem::path &path)
{
    try
    {
        auto file = fastgltf::GltfDataBuffer::FromPath(path);
        if (file.error() != fastgltf::Error::None)
        {
            throw std::runtime_error("Could not open or read GLB file");
        }
        fastgltf::Parser parser;
        auto parsed = parser.loadGltfBinary(file.get(), path.parent_path(), fastgltf::Options::GenerateMeshIndices);
        check(parsed.error());
        const auto &asset = parsed.get();
        check(fastgltf::validate(asset));
        const auto scene_index = asset.defaultScene.value_or(0);
        if (scene_index >= asset.scenes.size())
        {
            throw std::runtime_error("GLB has no valid scene to load");
        }
        const auto &scene = asset.scenes[scene_index];

        struct PendingNode
        {
            std::size_t index;
            fastgltf::math::nmat4x4 parent;
        };
        std::vector<PendingNode> pending;
        for (const auto root : scene.nodeIndices | std::views::reverse)
        {
            pending.push_back({root, fastgltf::math::nmat4x4{}});
        }
        GltfGeometry result;
        std::vector<std::optional<std::size_t>> mesh_slots(asset.meshes.size());
        std::vector<bool> visited(asset.nodes.size());
        while (!pending.empty())
        {
            const auto [index, parent] = pending.back();
            pending.pop_back();
            const auto &node = asset.nodes.at(index);
            if (visited[index])
            {
                throw std::runtime_error("Scene node appears more than once in the hierarchy");
            }
            visited[index] = true;
            const auto world = fastgltf::getTransformMatrix(node, parent);
            if (node.meshIndex)
            {
                if (node.skinIndex || !std::in_range<std::uint32_t>(index))
                {
                    throw std::runtime_error("Expected a static mesh node with a representable instance ID");
                }
                auto &slot = mesh_slots.at(*node.meshIndex);
                if (!slot)
                {
                    slot = result.meshes_.size();
                    result.meshes_.push_back(read_mesh(asset, asset.meshes.at(*node.meshIndex)));
                }
                result.instances_.push_back(
                    {InstanceId{static_cast<std::uint32_t>(index)}, result.meshes_[*slot], instance_transform(world)});
            }
            for (const auto child : node.children | std::views::reverse)
            {
                pending.push_back({child, world});
            }
        }
        return result;
    }
    catch (const std::exception &error)
    {
        return std::unexpected(std::format("{}: {}", path.string(), error.what()));
    }
}
} // namespace formlight
