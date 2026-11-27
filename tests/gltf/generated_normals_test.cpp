import std;
import formlight.gltf;

namespace
{
using namespace formlight;

void near(Vec3 actual, Vec3 expected, std::size_t face, std::size_t corner)
{
    const auto d = abs_components(actual - expected);
    if (!(std::max({d.x, d.y, d.z}) < 1e-6))
    {
        throw std::runtime_error(std::format("Face {}, corner {}: got ({}, {}, {}), expected ({}, {}, {})", face,
                                             corner, actual.x, actual.y, actual.z, expected.x, expected.y, expected.z));
    }
}

void check_normals(const std::filesystem::path &path, GltfContent content)
{
    const auto scene = GltfScene::load(path, content);
    if (!scene)
    {
        throw std::runtime_error(scene.error());
    }
    const auto &instance = scene->instance(InstanceId{0});
    const auto faces = instance.triangles;
    if (faces.size() != 15)
    {
        throw std::runtime_error(std::format("Expected 15 triangles, got {}", faces.size()));
    }

    // The two sides of the fold have areas 2 (+Z) and 1 (+Y).
    const Vec3 x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};
    const Vec3 shared = Vec3{0, 1, 2} / std::sqrt(5.0);
    const auto check_face = [&](std::size_t i, Vec3 a, Vec3 b, Vec3 c)
    {
        near(faces[i].vertices[0].normal, a, i, 0);
        near(faces[i].vertices[1].normal, b, i, 1);
        near(faces[i].vertices[2].normal, c, i, 2);
    };
    check_face(0, shared, shared, z); // List
    check_face(1, shared, shared, y);
    check_face(2, z, shared, shared); // Strip
    check_face(3, shared, shared, y);
    check_face(4, shared, y, shared); // Fan
    check_face(5, shared, shared, z);
    check_face(6, z, z, z); // Split indices at the fold
    check_face(7, y, y, y);
    check_face(8, x, x, x); // Authored normals
    check_face(9, x, x, x);
    check_face(10, z, z, z); // Opposite winding cancels at shared vertices
    check_face(11, z * -1, z * -1, z * -1);
    check_face(12, z, z, z);           // Isolated zero-area triangle
    check_face(13, shared, shared, z); // Normal-mapped material
    check_face(14, shared, shared, y);

    if (content == GltfContent::Materials)
    {
        const auto world_normal = scene->shading_normal(instance, faces[0], {0.25, 0.25, 0.5}, {}, false);
        near(world_normal, unit_vector(shared + z, "Invalid expected normal"), 0, 2);
        for (const std::size_t face : {13u, 14u})
        {
            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                const auto &v = faces[face].vertices[corner];
                if (!(std::abs(dot(v.normal, v.tangent)) < 1e-6 && std::abs(dot(v.tangent, v.tangent) - 1) < 1e-6))
                {
                    throw std::runtime_error(
                        std::format("Face {}, corner {} has an invalid tangent frame", face, corner));
                }
            }
        }
    }
}
} // namespace

int main(int argc, char **argv)
try
{
    if (argc != 2)
    {
        return 1;
    }
    check_normals(argv[1], formlight::GltfContent::Geometry);
    check_normals(argv[1], formlight::GltfContent::Materials);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
