module;

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

export module formlight.gltf_mesh;

import std;
import formlight.gltf;
import formlight.gltf_resources;
import formlight.gltf_tangents;

namespace formlight::gltf_detail
{
namespace
{
void read_vertex_colors(const fastgltf::Asset &asset, const fastgltf::Accessor &colors,
                        std::span<TriangleVertex> vertices)
{
    if (colors.count != vertices.size())
    {
        throw std::runtime_error("COLOR_0 and POSITION have different vertex counts");
    }
    if (colors.type == fastgltf::AccessorType::Vec3)
    {
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, colors, [&](auto rgb, std::size_t i)
                                                                  { vertices[i].color = {rgb[0], rgb[1], rgb[2], 1}; });
    }
    else if (colors.type == fastgltf::AccessorType::Vec4)
    {
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
            asset, colors, [&](auto rgba, std::size_t i) { vertices[i].color = {rgba[0], rgba[1], rgba[2], rgba[3]}; });
    }
    else
    {
        throw std::runtime_error("COLOR_0 must supply VEC3 or VEC4 colors");
    }
}

void read_texcoords(ModelResources &resources, const fastgltf::Primitive &primitive, std::span<TriangleVertex> vertices,
                    GltfContent content, std::size_t mesh_index, std::size_t primitive_index)
{
    const auto &asset = resources.asset;
    constexpr std::array<std::string_view, 2> names{"TEXCOORD_0", "TEXCOORD_1"};
    std::array<bool, names.size()> present{};
    for (std::size_t set = 0; set < names.size(); ++set)
    {
        const auto name = names[set];
        const auto attribute = primitive.findAttribute(name);
        if (attribute == primitive.attributes.end())
        {
            continue;
        }
        present[set] = true;
        const auto &uvs = resources.geometry_accessor(attribute->accessorIndex);
        if (uvs.count != vertices.size() || uvs.type != fastgltf::AccessorType::Vec2)
        {
            throw std::runtime_error(std::format("Mesh {} primitive {}: {} must supply one VEC2 per position",
                                                 mesh_index, primitive_index, name));
        }
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
            asset, uvs,
            [&](auto uv, std::size_t i)
            {
                if (!std::isfinite(uv[0]) || !std::isfinite(uv[1]))
                {
                    throw std::runtime_error(std::format("Mesh {} primitive {}: {} contains a non-finite coordinate",
                                                         mesh_index, primitive_index, name));
                }
                vertices[i].texcoords[set] = {uv[0], uv[1]};
            });
    }
    if (content == GltfContent::Materials && primitive.materialIndex)
    {
        const auto &material = asset.materials.at(*primitive.materialIndex);
        const auto require = [&](std::string_view slot, const auto &binding)
        {
            if (!binding)
            {
                return;
            }
            const auto set = binding->texCoordIndex;
            if (set >= present.size() || !present[set])
            {
                throw std::runtime_error(std::format("Mesh {} primitive {} material {}: {} requires {} TEXCOORD_{}",
                                                     mesh_index, primitive_index, *primitive.materialIndex, slot,
                                                     set >= present.size() ? "unsupported" : "missing", set));
            }
        };
        require("baseColorTexture", material.pbrData.baseColorTexture);
        require("metallicRoughnessTexture", material.pbrData.metallicRoughnessTexture);
        require("normalTexture", material.normalTexture);
        require("occlusionTexture", material.occlusionTexture);
        require("emissiveTexture", material.emissiveTexture);
    }
}

struct IndexedFaces
{
    fastgltf::PrimitiveType mode;
    std::vector<std::uint32_t> indices;
    std::size_t count;

    [[nodiscard]] std::array<std::uint32_t, 3> operator[](std::size_t face) const
    {
        using enum fastgltf::PrimitiveType;
        switch (mode)
        {
        case Triangles:
            return {indices[3 * face], indices[3 * face + 1], indices[3 * face + 2]};
        case TriangleFan:
            return {indices[0], indices[face + 1], indices[face + 2]};
        case TriangleStrip:
            if (face % 2 == 0)
            {
                return {indices[face], indices[face + 1], indices[face + 2]};
            }
            return {indices[face + 1], indices[face], indices[face + 2]};
        default:
            throw std::runtime_error("Expected TRIANGLES, TRIANGLE_STRIP or TRIANGLE_FAN");
        }
    }
};

IndexedFaces read_indices(ModelResources &resources, const fastgltf::Primitive &primitive, std::size_t vertex_count,
                          std::size_t previous_faces)
{
    // GenerateMeshIndices supplies consecutive indices for non-indexed primitives.
    const auto &accessor = resources.geometry_accessor(primitive.indicesAccessor.value());
    using enum fastgltf::ComponentType;
    if (accessor.type != fastgltf::AccessorType::Scalar || accessor.normalized ||
        (accessor.componentType != UnsignedByte && accessor.componentType != UnsignedShort &&
         accessor.componentType != UnsignedInt))
    {
        throw std::runtime_error("Expected unsigned scalar triangle indices");
    }
    using enum fastgltf::PrimitiveType;
    const auto count = accessor.count;
    if (primitive.type == Triangles && count % 3 != 0)
    {
        throw std::runtime_error("TRIANGLES index count must be a multiple of three");
    }
    const auto face_count = primitive.type == Triangles ? count / 3 : std::max(count, std::size_t{2}) - 2;
    if (face_count > std::numeric_limits<std::uint32_t>::max() - previous_faces)
    {
        throw std::runtime_error("Too many triangles for mesh-local primitive IDs");
    }

    std::vector<std::uint32_t> indices(count);
    const auto read_index = [&](std::uint32_t index, std::size_t i)
    {
        if (index >= vertex_count)
        {
            throw std::runtime_error("Triangle index exceeds POSITION count");
        }
        indices[i] = index;
    };
    fastgltf::iterateAccessorWithIndex<std::uint32_t>(resources.asset, accessor, read_index);

    return {primitive.type, std::move(indices), face_count};
}

void generate_area_weighted_normals(const IndexedFaces &faces, std::span<TriangleVertex> vertices)
{
    for (std::size_t face = 0; face < faces.count; ++face)
    {
        const auto corners = faces[face];
        const auto [a, b, c] = corners;
        // The cross product's magnitude is twice the face area.
        const auto n = cross(vertices[b].position - vertices[a].position, vertices[c].position - vertices[a].position);
        for (const auto index : corners)
        {
            vertices[index].normal = vertices[index].normal + n;
        }
    }
    for (auto &v : vertices)
    {
        const auto length = std::hypot(v.normal.x, v.normal.y, v.normal.z);
        if (length > 0)
        {
            v.normal = v.normal / length;
        }
    }
}

void append_triangles(std::vector<Triangle> &triangles, const IndexedFaces &faces,
                      std::span<const TriangleVertex> vertices, std::size_t material_slot)
{
    const auto start = triangles.size();
    triangles.resize(start + faces.count);
    for (std::size_t face = 0; face < faces.count; ++face)
    {
        const auto [a, b, c] = faces[face];
        auto &triangle = triangles[start + face];
        triangle = {
            .id = PrimitiveId{static_cast<std::uint32_t>(start + face)},
            .vertices = {vertices[a], vertices[b], vertices[c]},
            .material_index = material_slot,
        };
        // Opposite faces can cancel at a shared vertex. Use this face's direction at that corner.
        for (auto &corner : triangle.vertices)
        {
            if (corner.normal == Vec3{})
            {
                const auto n = triangle.geometric_normal();
                const auto length = std::hypot(n.x, n.y, n.z);
                corner.normal = length > 0 ? n / length : Vec3{0, 0, 1};
            }
        }
    }
}

} // namespace

export std::vector<Triangle> read_mesh(ModelResources &resources, std::size_t mesh_index, GltfContent content)
{
    const auto &asset = resources.asset;
    const auto &mesh = asset.meshes.at(mesh_index);
    std::vector<Triangle> triangles;
    for (std::size_t primitive_index = 0; primitive_index < mesh.primitives.size(); ++primitive_index)
    {
        const auto &primitive = mesh.primitives[primitive_index];
        using enum fastgltf::PrimitiveType;
        if (primitive.type != Triangles && primitive.type != TriangleStrip && primitive.type != TriangleFan)
        {
            throw std::runtime_error("Expected TRIANGLES, TRIANGLE_STRIP or TRIANGLE_FAN");
        }
        if (!primitive.targets.empty())
        {
            throw std::runtime_error("Morph targets are not supported");
        }
        const auto position = primitive.findAttribute("POSITION");
        if (position == primitive.attributes.end())
        {
            throw std::runtime_error("Triangle primitive is missing POSITION");
        }
        const auto &positions = resources.geometry_accessor(position->accessorIndex);
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
        const auto normal = primitive.findAttribute("NORMAL");
        if (normal != primitive.attributes.end())
        {
            const auto &normals = resources.geometry_accessor(normal->accessorIndex);
            if (normals.count != positions.count || normals.type != fastgltf::AccessorType::Vec3)
            {
                throw std::runtime_error("NORMAL must supply one VEC3 per position");
            }
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                asset, normals, [&](auto n, std::size_t i)
                { vertices[i].normal = unit_vector({n[0], n[1], n[2]}, "NORMAL must be finite and nonzero"); });
        }
        read_texcoords(resources, primitive, vertices, content, mesh_index, primitive_index);
        const auto tangent = primitive.findAttribute("TANGENT");
        const bool use_tangents = content == GltfContent::Materials && normal != primitive.attributes.end() &&
                                  tangent != primitive.attributes.end();
        if (use_tangents)
        {
            const auto &accessor = resources.geometry_accessor(tangent->accessorIndex);
            if (accessor.type != fastgltf::AccessorType::Vec4 || accessor.count != vertices.size())
            {
                throw std::runtime_error("TANGENT must supply one VEC4 per position");
            }
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
                asset, accessor,
                [&](auto t, std::size_t i)
                {
                    if (t[3] != 1 && t[3] != -1)
                    {
                        throw std::runtime_error("TANGENT.w must be +1 or -1");
                    }
                    vertices[i].tangent = unit_vector({t[0], t[1], t[2]}, "TANGENT must be finite and nonzero");
                    vertices[i].tangent_sign = t[3];
                });
        }
        if (const auto color = primitive.findAttribute("COLOR_0");
            content == GltfContent::Materials && color != primitive.attributes.end())
        {
            read_vertex_colors(asset, resources.geometry_accessor(color->accessorIndex), vertices);
        }
        const auto start = triangles.size();
        const auto faces = read_indices(resources, primitive, vertices.size(), start);
        if (normal == primitive.attributes.end())
        {
            generate_area_weighted_normals(faces, vertices);
        }
        const auto material_slot = primitive.materialIndex ? *primitive.materialIndex + 1 : 0;
        append_triangles(triangles, faces, vertices, material_slot);
        if (content != GltfContent::Materials || use_tangents || !primitive.materialIndex)
        {
            continue;
        }
        if (const auto &map = asset.materials.at(*primitive.materialIndex).normalTexture)
        {
            generate_tangents(std::span{triangles}.subspan(start), map->texCoordIndex);
        }
    }
    return triangles;
}
} // namespace formlight::gltf_detail
