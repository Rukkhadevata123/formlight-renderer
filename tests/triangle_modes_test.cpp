import std;
import formlight.gltf;

using namespace formlight;

int main(int argc, char **argv)
try
{
    if (argc != 2)
    {
        return 1;
    }
    const auto scene = GltfScene::load(argv[1], GltfContent::Materials);
    if (!scene)
    {
        throw std::runtime_error(scene.error());
    }
    const auto faces = scene->instances().front().triangles;
    constexpr std::array<Vec3, 4> positions{{{-0.5, -0.5, 0}, {0.5, -0.5, 0}, {-0.5, 0.5, 0}, {0.5, 0.5, 0}}};
    std::size_t index = 0;
    const auto check = [&](std::size_t slot, std::initializer_list<std::array<unsigned, 3>> expected)
    {
        for (const auto &corners : expected)
        {
            if (index >= faces.size())
            {
                throw std::runtime_error(std::format("Missing triangle {}", index));
            }
            const auto &face = faces[index];
            if (std::to_underlying(face.id) != index || face.material_index != slot)
            {
                throw std::runtime_error(std::format("Triangle {} has the wrong ID or material slot", index));
            }
            for (std::size_t i = 0; i < corners.size(); ++i)
            {
                if (face.vertices[i].position != positions[corners[i]])
                {
                    throw std::runtime_error(
                        std::format("Triangle {}, corner {} should use vertex {}", index, i, corners[i]));
                }
            }
            ++index;
        }
    };
    check(0, {{0, 1, 2}});            // Non-indexed list
    check(0, {{0, 1, 2}});            // Indexed list
    check(1, {{0, 1, 2}, {2, 1, 3}}); // Indexed strip
    check(2, {{0, 1, 3}, {0, 3, 2}}); // Indexed fan
    check(0, {{0, 1, 2}, {2, 1, 3}}); // Non-indexed strip
    check(0, {{0, 1, 2}, {0, 2, 3}}); // Non-indexed fan

    // Repeated indices still advance strip parity. The fan anchors at source vertex 2.
    check(1, {{2, 1, 1}, {1, 1, 0}, {1, 0, 3}, {3, 0, 2}});
    check(2, {{2, 1, 1}, {2, 1, 0}, {2, 0, 3}, {2, 3, 2}});
    if (faces.size() != index)
    {
        throw std::runtime_error(std::format("Expected {} triangles, got {}", index, faces.size()));
    }
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
