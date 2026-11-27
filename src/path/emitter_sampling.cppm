export module formlight.emitter_sampling;

import std;
import formlight.sampling;
import formlight.ray_query;

namespace formlight
{
// Borrows a static scene. Triangle selection and point selection together are uniform in world area.
export class EmitterSampler
{
  public:
    EmitterSampler(std::span<const MeshInstance> instances, auto emits)
    {
        for (const auto &instance : instances)
        {
            for (const auto &face : instance.triangles)
            {
                if (!emits(face))
                {
                    continue;
                }
                const auto &[a, b, c] = face.vertices;
                const Vec3 ab = instance.transform.to_world_vector(b.position - a.position);
                const Vec3 ac = instance.transform.to_world_vector(c.position - a.position);
                const Vec3 cross_product = cross(ab, ac);
                const double twice_area = std::hypot(cross_product.x, cross_product.y, cross_product.z);
                if (twice_area == 0)
                {
                    continue;
                }
                const double next = total_area_ + 0.5 * twice_area;
                if (!std::isfinite(next))
                {
                    throw std::invalid_argument("Emissive mesh area exceeds working precision");
                }
                if (next > total_area_)
                {
                    targets_.push_back({&instance, &face, cross_product / twice_area, next});
                    total_area_ = next;
                }
            }
        }
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return targets_.empty();
    }

    [[nodiscard]] std::optional<Vec3> sample(Vec3 origin, PathSampler &sampler) const
    {
        if (empty())
        {
            return std::nullopt;
        }
        const double draw = std::min(sampler.uniform() * total_area_, std::nextafter(total_area_, 0.0));
        const auto target = std::ranges::upper_bound(targets_, draw, {}, &Target::cumulative_area);
        const double root = std::sqrt(sampler.uniform());
        const double b = root * sampler.uniform();
        const Vec3 local = target->face->position_at({1 - root, b, root - b});
        const Vec3 delta = target->instance->transform.to_world_point(local) - origin;
        const double distance = std::hypot(delta.x, delta.y, delta.z);
        // A target seen exactly edge-on spans zero solid angle. This draw contributes zero.
        if (!(distance > 0) || !std::isfinite(distance) || dot(target->normal, delta) == 0)
        {
            return std::nullopt;
        }
        return delta / distance;
    }

    // Different points on overlapping targets can generate the same unit direction, even behind an occluder.
    [[nodiscard]] double density(const Ray &unit_ray) const
    {
        double pdf = 0;
        for (const auto &target : targets_)
        {
            const double cosine = std::abs(dot(target.normal, unit_ray.direction));
            if (cosine == 0)
            {
                continue;
            }
            const auto &instance = *target.instance;
            const auto hit = intersect_triangle(instance.transform.to_local_ray(unit_ray), *target.face, instance.id,
                                                {0, std::numeric_limits<double>::infinity()}, {});
            if (hit)
            {
                pdf += (hit->t / total_area_) * (hit->t / cosine);
            }
        }
        return pdf;
    }

  private:
    struct Target
    {
        const MeshInstance *instance;
        const Triangle *face;
        Vec3 normal;
        double cumulative_area;
    };
    std::vector<Target> targets_;
    double total_area_{};
};
} // namespace formlight
