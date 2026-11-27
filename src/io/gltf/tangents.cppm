module;
#include <mikktspace.h>

export module formlight.gltf_tangents;

import std;
import formlight.gltf;

namespace formlight::gltf_detail
{
namespace
{
struct MikkMesh
{
    std::span<Triangle> triangles;
    std::size_t uv_set{};
    std::optional<UvTransform> transform{};
};

MikkMesh &mesh(const SMikkTSpaceContext *context)
{
    return *static_cast<MikkMesh *>(context->m_pUserData);
}

TriangleVertex &corner(const SMikkTSpaceContext *context, int face, int vertex)
{
    return mesh(context).triangles[static_cast<std::size_t>(face)].vertices[static_cast<std::size_t>(vertex)];
}

void write_vector(float *out, Vec3 v)
{
    out[0] = static_cast<float>(v.x);
    out[1] = static_cast<float>(v.y);
    out[2] = static_cast<float>(v.z);
}
} // namespace

export void generate_tangents(std::span<Triangle> triangles, std::size_t uv_set, std::optional<UvTransform> transform)
{
    if (triangles.empty())
    {
        return;
    }
    // MikkTSpace packs a face index and two corner bits into a signed int.
    if (triangles.size() > static_cast<std::size_t>(std::numeric_limits<int>::max() / 4))
    {
        throw std::runtime_error("Too many triangles for MikkTSpace");
    }

    SMikkTSpaceInterface callbacks{};
    callbacks.m_getNumFaces = [](const SMikkTSpaceContext *c) { return static_cast<int>(mesh(c).triangles.size()); };
    callbacks.m_getNumVerticesOfFace = [](const SMikkTSpaceContext *, int) { return 3; };
    callbacks.m_getPosition = [](const SMikkTSpaceContext *c, float out[], int f, int v)
    { write_vector(out, corner(c, f, v).position); };
    callbacks.m_getNormal = [](const SMikkTSpaceContext *c, float out[], int f, int v)
    { write_vector(out, corner(c, f, v).normal); };
    callbacks.m_getTexCoord = [](const SMikkTSpaceContext *c, float out[], int f, int v)
    {
        const auto &input = mesh(c);
        auto uv = corner(c, f, v).texcoords[input.uv_set];
        if (input.transform)
        {
            uv = input.transform->point(uv);
        }
        out[0] = static_cast<float>(uv.u);
        out[1] = static_cast<float>(uv.v);
    };
    callbacks.m_setTSpaceBasic = [](const SMikkTSpaceContext *c, const float t[], float sign, int f, int v)
    {
        auto &vertex = corner(c, f, v);
        vertex.tangent = {t[0], t[1], t[2]};
        vertex.tangent_sign = sign;
    };
    MikkMesh input{triangles, uv_set, transform};
    const SMikkTSpaceContext context{&callbacks, &input};
    if (!genTangSpaceDefault(&context))
    {
        throw std::runtime_error("MikkTSpace tangent generation failed");
    }
}
} // namespace formlight::gltf_detail
