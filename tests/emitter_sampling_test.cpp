import std;
import formlight.ray_query;
import formlight.sampling;
import formlight.emitter_sampling;
import formlight.target_sampling;

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
void check_extra_target()
{
    const std::array mesh{
        Triangle{.vertices = {TriangleVertex{{-1, -1, 0}}, TriangleVertex{{1, -1, 0}}, TriangleVertex{{0, 1, 0}}}}};
    const std::array instances{MeshInstance{InstanceId{3}, mesh, {Basis3{}, {0, 0, 2}}, true}};
    const DirectionTargets sphere{instances, [](const Triangle &) { return false; }};
    const DirectionTargets overlap{instances, [](const Triangle &) { return true; }};
    // The triangle has area 2; its enclosing sphere has radius sqrt(2). From the origin the half angle is 45 degrees.
    const double cap_height = 1 - std::sqrt(0.5);
    const double sphere_pdf = 1 / (2 * std::numbers::pi * cap_height);
    const Ray central{{}, {0, 0, 1}};
    if (std::abs(overlap.density(central) - 0.5 * (2 + sphere_pdf)) > 1e-12 || sphere.density({{}, {1, 0, 0}}) != 0)
    {
        throw std::runtime_error("Overlapping emitter and instance target have incorrect mixture density");
    }

    PathSampler rng{7, 1, 0};
    double sum_z = 0;
    constexpr unsigned draws = 8192;
    for (unsigned i = 0; i < draws; ++i)
    {
        const auto d = sphere.sample({}, rng).value();
        if (std::abs(dot(d, d) - 1) > 1e-12 || std::abs(sphere.density({{}, d}) - sphere_pdf) > 1e-12)
        {
            throw std::runtime_error("Sampled target direction lies outside its uniform cone");
        }
        sum_z += d.z;
    }
    if (std::abs(sum_z / draws - (1 - 0.5 * cap_height)) > 0.004)
    {
        throw std::runtime_error("Cosine of the cone angle must be uniformly distributed");
    }

    const Vec3 center{0, 0, 2};
    const auto d = sphere.sample(center, rng).value();
    if (std::abs(dot(d, d) - 1) > 1e-12 || sphere.density({center, d}) != 1 / (4 * std::numbers::pi))
    {
        throw std::runtime_error("An interior receiver must sample the full sphere");
    }
}
} // namespace

int main()
try
{
    check_emitter_pdf();
    check_extra_target();
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
