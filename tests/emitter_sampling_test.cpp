import std;
import formlight.ray_query;
import formlight.sampling;
import formlight.emitter_sampling;

namespace
{
using namespace formlight;

void check_emitter_pdf()
{
    const std::array mesh{
        Triangle{.vertices = {TriangleVertex{{-1, -1, 0}}, TriangleVertex{{1, -1, 0}}, TriangleVertex{{0, 1, 0}}}}};
    // The two instances span the same solid angle; their areas are 2 and 8 and their distances are 2 and 4.
    const Basis3 mirrored{.x = {-2, 0, 0}, .y = {0, 2, 0}};
    std::array targets{MeshInstance{InstanceId{0}, mesh, {Basis3{}, {0, 0, 2}}},
                       MeshInstance{InstanceId{1}, mesh, {mirrored, {0, 0, 4}}}};
    const auto emits = [](const Triangle &) { return true; };
    {
        const EmitterSampler overlapping{targets, emits};
        if (std::abs(overlapping.density({{}, {0, 0, 1}}) - 2.0) > 1e-12 || overlapping.density({{}, {0, 0, -1}}) != 0)
        {
            throw std::runtime_error("Emitter PDF must sum all positive-distance preimages of a direction");
        }
    }
    targets[1].transform = {mirrored, {6, 0, 4}};
    const EmitterSampler separated{targets, emits};
    PathSampler rng{17, 0, 0};
    constexpr int samples = 32768;
    int far_count = 0;
    Vec3 centroid{};
    for (int i = 0; i < samples; ++i)
    {
        const Vec3 d = separated.sample({}, rng).value();
        const bool far = d.x / d.z > 0.75;
        const double t = (far ? 4 : 2) / d.z;
        const double expected_pdf = t * t / (10 * d.z);
        if (std::abs(dot(d, d) - 1) > 1e-12 || std::abs(separated.density({{}, d}) / expected_pdf - 1) > 1e-12)
        {
            throw std::runtime_error("Generated emitter direction and world-area PDF disagree");
        }
        far_count += far;
        if (far)
        {
            centroid = centroid + d * t;
        }
    }
    const auto offset = centroid / far_count - Vec3{6, -2.0 / 3, 4};
    if (std::abs(static_cast<double>(far_count) / samples - 0.8) > 0.015 ||
        std::hypot(offset.x, offset.y, offset.z) > 0.03)
    {
        throw std::runtime_error("Samples must select instances by area and cover the selected triangle uniformly");
    }
    const EmitterSampler empty{targets, [](const Triangle &) { return false; }};
    if (!empty.empty() || empty.sample({}, rng) || empty.density({{}, {0, 0, 1}}) != 0)
    {
        throw std::runtime_error("Empty emitter list produced a direction or nonzero density");
    }
    const EmitterSampler flat{std::span{targets}.first(1), emits};
    if (flat.sample({3, 0, 2}, rng) || flat.density({{3, 0, 2}, {-1, 0, 0}}) != 0)
    {
        throw std::runtime_error("An edge-on emitter has zero solid angle");
    }
}
} // namespace

int main()
try
{
    check_emitter_pdf();
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
