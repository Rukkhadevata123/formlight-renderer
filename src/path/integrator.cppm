export module formlight.path;

import std;
import formlight.gltf;
import formlight.ray_query;
import formlight.scene_config;
import formlight.sampling;

namespace formlight
{
export struct PathHit
{
    RayHit intersection;
    SurfaceMaterial material;
    bool back_face{};
    std::optional<Vec3> shading_normal{};
    std::vector<DiagnosticLight> lights{};
    std::optional<Vec3> bounce_radiance{};
};

// The scene must outlive the integrator. Primary rays see the authored front of single-sided materials.
export class PathIntegrator
{
  public:
    explicit PathIntegrator(const GltfScene &scene)
        : scene_(scene), surfaces_(std::make_shared<TriangleBvh>(scene.instances(), scene.solid_filter(),
                                                                 scene.face_culling(CullFace::Back)))
    {
    }

    void set_lighting(std::span<const ActiveLight> lights)
    {
        std::vector<std::pair<std::size_t, AreaLighting>> rectangles;
        for (const auto &entry : lights)
        {
            if (const auto *area = std::get_if<AreaLighting>(&entry.source))
            {
                if (area->shadows && !blockers_)
                {
                    // Shadow rays travel from the receiver to the lamp, opposite the light's view of a caster.
                    blockers_ = std::make_shared<TriangleBvh>(scene_.instances(), scene_.solid_filter(),
                                                              scene_.face_culling(CullFace::Front));
                }
                rectangles.emplace_back(entry.scene_index, *area);
            }
        }
        rectangles_ = std::move(rectangles);
    }

    [[nodiscard]] LinearColor radiance(const Ray &ray, RayInterval interval, std::optional<PathHit> *record = nullptr,
                                       std::uint64_t pixel = 0,
                                       AreaLightPattern pattern = AreaLightPattern::ShiftedHalton,
                                       std::stop_token stop = {}, PathSampling sampling = {}) const
    {
        if (sampling.samples == 0)
        {
            throw std::invalid_argument("Path sample count must be positive");
        }
        const auto hit = surfaces_->intersect(ray, interval);
        if (!hit)
        {
            if (record)
            {
                record->reset();
            }
            return {}; // Black environment.
        }
        const auto surface = interaction(*hit, ray);
        PathHit *first = record ? &record->emplace(*hit, surface.material, surface.back_face) : nullptr;
        if (first)
        {
            first->shading_normal = surface.normal;
        }
        const Vec3 direct = direct_light(surface, ray, pixel, pattern, stop, first);
        if (sampling.max_bounces == 0)
        {
            return surface.material.outgoing_color(direct);
        }
        Vec3 sum{};
        for (std::uint32_t i = 0; i < sampling.samples; ++i)
        {
            PathSampler sampler{sampling.seed, pixel, i};
            sum = sum + diffuse_path(surface, ray.direction * -1, sampler, sampling.max_bounces, pattern, stop);
        }
        const Vec3 bounced = sum / sampling.samples;
        if (first)
        {
            first->bounce_radiance = bounced;
        }
        return surface.material.outgoing_color(direct + bounced);
    }

  private:
    struct Interaction
    {
        SurfaceMaterial material;
        SurfaceRayOrigin origin;
        Vec3 normal;
        bool back_face;
    };

    [[nodiscard]] Interaction interaction(const RayHit &hit, const Ray &ray) const
    {
        const auto &[id, weights, gradients] = hit.surface;
        const auto &instance = scene_.instance(id.instance);
        const auto &face = instance.triangles[std::to_underlying(id.primitive)];
        const bool back = dot(face.geometric_normal(), instance.transform.to_local_vector(ray.direction)) > 0;
        return {scene_.surface_material(face, weights, gradients), SurfaceRayOrigin{instance, face, weights},
                scene_.shading_normal(instance, face, weights, gradients, back), back};
    }

    [[nodiscard]] Vec3 direct_light(const Interaction &surface, const Ray &ray, std::uint64_t pixel,
                                    AreaLightPattern pattern, std::stop_token stop, PathHit *first = nullptr) const
    {
        if (rectangles_.empty())
        {
            return {};
        }
        const MetallicRoughnessBrdf brdf{surface.material, surface.normal, ray.direction * -1};
        Vec3 direct{};
        for (const auto &[index, rectangle] : rectangles_)
        {
            auto *light_record = first ? &first->lights.emplace_back() : nullptr;
            const Vec3 contribution =
                rectangle.evaluate(surface.origin, brdf, pixel, blockers_.get(), pattern, light_record, stop);
            direct = direct + contribution;
            if (light_record)
            {
                light_record->scene_index = index;
                light_record->direct = contribution;
            }
        }
        return direct;
    }

    [[nodiscard]] Vec3 diffuse_path(Interaction surface, Vec3 outgoing, PathSampler &sampler, std::uint32_t max_bounces,
                                    AreaLightPattern pattern, std::stop_token stop) const
    {
        Vec3 radiance{}, throughput{1, 1, 1};
        for (std::uint32_t depth = 0; depth < max_bounces; ++depth)
        {
            if (stop.stop_requested())
            {
                throw std::runtime_error("Rendering cancelled");
            }
            // Lambertian f*cos/pdf equals diffuse albedo for cosine-weighted sampling.
            const auto &base = surface.material.base_color;
            throughput = Vec3{throughput.x * base.r, throughput.y * base.g, throughput.z * base.b} *
                         (1 - surface.material.metallic);
            if (throughput == Vec3{} || dot(surface.normal, outgoing) <= 0)
            {
                break;
            }
            const double u = sampler.uniform();
            const double v = sampler.uniform();
            const Vec3 direction = cosine_hemisphere(surface.normal, u, v);
            const Vec3 geometric = surface.origin.geometric_normal() * (surface.back_face ? -1 : 1);
            if (dot(direction, geometric) <= 0)
            {
                break;
            }
            const Ray ray = surface.origin.along(direction);
            const auto hit = surfaces_->intersect(ray, {0, std::numeric_limits<double>::infinity()});
            if (!hit)
            {
                break;
            }
            surface = interaction(*hit, ray);
            outgoing = ray.direction * -1;
            const auto &e = surface.material.emission;
            const Vec3 incoming = Vec3{e.r, e.g, e.b} + direct_light(surface, ray, sampler.next_bits(), pattern, stop);
            radiance = radiance + Vec3{throughput.x * incoming.x, throughput.y * incoming.y, throughput.z * incoming.z};
        }
        return radiance;
    }

    const GltfScene &scene_;
    std::shared_ptr<const TriangleBvh> surfaces_, blockers_;
    std::vector<std::pair<std::size_t, AreaLighting>> rectangles_;
};
} // namespace formlight
