export module formlight.target_sampling;

import std;
import formlight.ray_query;
import formlight.sampling;
import formlight.emitter_sampling;

namespace formlight
{
struct DirectionCone
{
    Vec3 axis;
    double height; // 1 - cos(half angle), or 2 for the full sphere.

    Vec3 sample(PathSampler &rng) const
    {
        const double h = height * rng.uniform();
        return polar_direction(axis, std::sqrt(h * (2 - h)), 1 - h, 2 * std::numbers::pi * rng.uniform());
    }

    double density(Vec3 unit_direction) const
    {
        const auto chord = unit_direction - axis;
        return height == 2 || dot(chord, chord) <= 2 * height ? 1 / (2 * std::numbers::pi * height) : 0;
    }
};

struct TargetSphere
{
    Sphere shape;
    explicit TargetSphere(const Sphere &sphere) : shape(sphere)
    {
    }

    explicit TargetSphere(const MeshInstance &instance) : shape{world_bounds(std::span{&instance, 1}).center(), 0}
    {
        for (const auto &face : instance.triangles)
        {
            for (const auto &vertex : face.vertices)
            {
                const Vec3 d = instance.transform.to_world_point(vertex.position) - shape.center;
                shape.radius = std::max(shape.radius, std::hypot(d.x, d.y, d.z));
            }
        }
    }

    DirectionCone cone(Vec3 origin, double time) const
    {
        const double radius = shape.radius;
        const Vec3 delta = shape.center_at(time) - origin;
        const double distance = std::hypot(delta.x, delta.y, delta.z);
        if (distance < radius)
        {
            return {{0, 0, 1}, 2};
        }
        const double sine = radius / distance;
        const double cosine = std::sqrt((1 - sine) * (1 + sine));
        // Rationalized to preserve the solid angle of distant targets.
        const double height = sine * sine / (1 + cosine);
        constexpr double epsilon = std::numeric_limits<double>::epsilon();
        if (!(height > epsilon * epsilon))
        {
            // This cone is narrower than the precision of its world-space unit directions.
            return {{0, 0, 1}, 2};
        }
        return {delta / distance, height};
    }
};

// One proposal covers all emitting triangles by area; each additional instance supplies a solid-angle proposal.
export class DirectionTargets
{
  public:
    DirectionTargets(std::span<const MeshInstance> instances, auto emits, std::span<const SphereInstance> spheres = {})
        : emitters_(instances, emits)
    {
        for (const auto &instance : instances)
        {
            if (instance.sampling_target)
            {
                spheres_.emplace_back(instance);
            }
        }
        for (const auto &sphere : spheres)
        {
            if (sphere.sampling_target)
            {
                spheres_.emplace_back(sphere.shape);
            }
        }
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return emitters_.empty() && spheres_.empty();
    }

    [[nodiscard]] std::optional<Vec3> sample(Vec3 origin, PathSampler &rng, double time = 0) const
    {
        if (spheres_.empty())
        {
            return emitters_.sample(origin, rng);
        }
        const auto count = proposal_count();
        const auto index = count == 1 ? 0 : static_cast<std::size_t>(rng.uniform() * static_cast<double>(count));
        if (index < spheres_.size())
        {
            return spheres_[index].cone(origin, time).sample(rng);
        }
        return emitters_.sample(origin, rng);
    }

    [[nodiscard]] double density(const Ray &unit_ray) const
    {
        double pdf = emitters_.density(unit_ray);
        if (spheres_.empty())
        {
            return pdf;
        }
        for (const auto &sphere : spheres_)
        {
            pdf += sphere.cone(unit_ray.origin, unit_ray.time).density(unit_ray.direction);
        }
        return pdf / static_cast<double>(proposal_count());
    }

  private:
    std::size_t proposal_count() const noexcept
    {
        return spheres_.size() + !emitters_.empty();
    }

    EmitterSampler emitters_;
    std::vector<TargetSphere> spheres_;
};
} // namespace formlight
