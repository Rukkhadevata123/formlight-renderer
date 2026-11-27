import std;
import formlight.shader;
import formlight.image_codec;

namespace
{
using namespace formlight;

struct Paint
{
    Vec3 rgb;
    double u;
};

Paint operator*(Paint p, double weight)
{
    return {p.rgb * weight, p.u * weight};
}

Paint operator+(Paint a, Paint b)
{
    return {a.rgb + b.rgb, a.u + b.u};
}

struct StripeShader
{
    using Varying = Paint;

    VertexOutput<Paint> vertex(const TriangleVertex &v) const
    {
        const Vec3 position{v.position.x + 0.15 * v.position.y, v.position.y, v.position.z};
        return {position, {{v.color.r, v.color.g, v.color.b}, v.texcoords[0].u}};
    }

    std::optional<LinearColor> fragment(Paint paint) const
    {
        if (paint.u > 0.43 && paint.u < 0.49)
        {
            return std::nullopt;
        }
        return LinearColor{static_cast<float>(paint.rgb.x), static_cast<float>(paint.rgb.y),
                           static_cast<float>(paint.rgb.z), 1};
    }
};
} // namespace

int main(int argc, char **argv)
try
{
    if (argc != 2)
    {
        std::cerr << "Usage: custom_shader OUTPUT.png\n";
        return 1;
    }
    const PinholeCamera camera{{{}, {0, 0, -1}, {0, 1, 0}, std::numbers::pi / 2}, {320, 240}};
    const TriangleVertex red{.position = {-2.1, -1.45, -2.3}, .color = {1, 0.03f, 0.03f}};
    const TriangleVertex green{
        .position = {2.33, -1.3, -2.6}, .texcoords = {TexCoord{1, 0}}, .color = {0.02f, 1, 0.08f}};
    const TriangleVertex blue{.position = {0, 1.24, -0.6}, .texcoords = {TexCoord{0.5, 1}}, .color = {0.02f, 0.12f, 1}};
    const std::array mesh{Triangle{.vertices = {red, green, blue}}};
    const std::array instances{MeshInstance{InstanceId{0}, mesh}};
    const auto image = render_shader(camera, instances, {1, 4}, StripeShader{}, {0.02f, 0.025f, 0.04f});
    write_png(argv[1], image);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
