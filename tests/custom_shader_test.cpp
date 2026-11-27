import std;
import formlight.shader;
import formlight.ray_query;

namespace
{
using namespace formlight;

constexpr LinearColor backing{0.125f, 0.25f, 0.5f};
constexpr ViewDepthRange full{0.5, 5}, clipped{1.3, 5};

struct RampShader
{
    using Varying = double;

    VertexOutput<double> vertex(const TriangleVertex &v) const
    {
        const auto [x, y, z] = v.position;
        return {{x + 0.4 * y, y, z}, v.texcoords[0].u};
    }

    std::optional<LinearColor> fragment(double u) const
    {
        if (u < 0)
        {
            return backing;
        }
        if (u > 0.314 && u < 0.438)
        {
            return std::nullopt;
        }
        return LinearColor{static_cast<float>(u), 0, 1};
    }
};

void check_color(LinearColor actual, LinearColor expected, int x, int y)
{
    if (!(std::abs(actual.r - expected.r) < 1e-6f && actual.g == expected.g && actual.b == expected.b &&
          actual.a == expected.a))
    {
        throw std::runtime_error(std::format("Pixel ({}, {}): expected ({}, {}, {}), got ({}, {}, {})", x, y,
                                             expected.r, expected.g, expected.b, actual.r, actual.g, actual.b));
    }
}

void check_draw_order(const PinholeCamera &camera, std::span<const MeshInstance> scene, const MeshInstance &reference)
{
    const auto original = render_shader(camera, scene, full, RampShader{});
    const auto cut = render_shader(camera, scene, clipped, RampShader{});
    int trimmed = 0, shaded = 0, discarded = 0;
    const auto [width, height] = camera.extent();
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto ray = camera.ray_for_sample(pixel_center(x, y));
            const auto hit = intersect(ray, std::span{&reference, 1}, {0, std::numeric_limits<double>::infinity()});
            LinearColor expected = backing;
            bool inside_cut = false;
            if (hit)
            {
                inside_cut = clipped.contains(camera.to_view(ray.at(hit->t)).depth);
                const auto w = hit->surface.weights;
                const double u = w.b + 0.2 * w.c;
                if (u <= 0.314 || u >= 0.438)
                {
                    expected = {static_cast<float>(u), 0, 1};
                    shaded += inside_cut;
                    trimmed += !inside_cut;
                }
                else if (inside_cut)
                {
                    ++discarded;
                }
            }
            check_color(original[y, x], expected, x, y);
            check_color(cut[y, x], inside_cut ? expected : backing, x, y);
        }
    }
    if (shaded == 0 || discarded == 0 || trimmed == 0)
    {
        throw std::runtime_error("Expected a visible ramp, a discard stripe and a near-plane cut");
    }
    std::println("{} shaded, {} discarded, {} colored pixels removed by clipping", shaded, discarded, trimmed);
}
} // namespace

int main()
try
{
    const PinholeCamera camera{{{}, {0, 0, -1}, {0, 1, 0}, std::numbers::pi / 2}, {64, 48}};
    const TriangleVertex a{{-1.5, -0.95, -2.3}}, b{{1.8, -1.1, -2.7}, {TexCoord{1, 0}}},
        c{{0.17, 0.96, -0.65}, {TexCoord{0.2, 0}}};
    const std::array front{Triangle{.vertices = {a, b, c}}};
    // Independently baked positions after the vertex shader's shear, for the ray reference.
    const std::array baked{
        Triangle{.vertices = {TriangleVertex{{-1.88, -0.95, -2.3}}, TriangleVertex{{1.36, -1.1, -2.7}},
                              TriangleVertex{{0.554, 0.96, -0.65}}}}};
    const std::array back{Triangle{.vertices = {TriangleVertex{{-30, -20, -4}, {TexCoord{-1, 0}}},
                                                TriangleVertex{{30, -20, -4}, {TexCoord{-1, 0}}},
                                                TriangleVertex{{0, 40, -4}, {TexCoord{-1, 0}}}}}};
    const AffineTransform placement{{}, {0.21, 0.08, 0}};
    const MeshInstance reference{InstanceId{1}, baked, placement};
    std::array scene{MeshInstance{InstanceId{1}, front, placement}, MeshInstance{InstanceId{2}, back}};
    check_draw_order(camera, scene, reference);
    std::ranges::reverse(scene);
    check_draw_order(camera, scene, reference);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
