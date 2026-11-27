export module formlight.path;

import std;
import formlight.gltf;
import formlight.ray_query;
import formlight.scene_config;
import formlight.sampling;
import formlight.target_sampling;

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

// The scene must outlive the integrator. Closed dielectrics expose both interfaces to path rays.
export class PathIntegrator
{
  public:
    explicit PathIntegrator(const GltfScene &scene)
        : scene_(scene),
          surfaces_(std::make_shared<TriangleBvh>(
              scene.instances(), scene.solid_filter(),
              [&scene](SurfaceId id)
              {
                  const auto &face = scene.instance(id.instance).triangles[std::to_underlying(id.primitive)];
                  const auto &mat = scene.material(face);
                  return mat.double_sided || mat.path_surface == PathSurface::Dielectric ? CullFace::None
                                                                                         : CullFace::Back;
              })),
          targets_(std::make_shared<DirectionTargets>(scene.instances(),
                                                      [&scene](const Triangle &face)
                                                      {
                                                          const auto &material = scene.material(face);
                                                          const auto &e = material.emission_multiplier;
                                                          return material.alpha_mode != AlphaMode::Blend &&
                                                                 (e.r > 0 || e.g > 0 || e.b > 0);
                                                      }))
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
            sum = sum + trace_bounces(surface, ray.direction, sampler, sampling, pattern, stop);
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
        PathSurface type;
        double ior;
    };

    [[nodiscard]] Interaction interaction(const RayHit &hit, const Ray &ray) const
    {
        const auto &[id, weights, gradients] = hit.surface;
        const auto &instance = scene_.instance(id.instance);
        const auto &face = instance.triangles[std::to_underlying(id.primitive)];
        const bool back = dot(face.geometric_normal(), instance.transform.to_local_vector(ray.direction)) > 0;
        const auto &mat = scene_.material(face);
        auto surface = scene_.surface_material(face, weights, gradients);
        if (back && !mat.double_sided)
        {
            surface.emission = {};
        }
        return {surface,
                SurfaceRayOrigin{instance, face, weights},
                scene_.shading_normal(instance, face, weights, gradients, back),
                back,
                mat.path_surface,
                mat.ior};
    }

    [[nodiscard]] Vec3 direct_light(const Interaction &surface, const Ray &ray, std::uint64_t pixel,
                                    AreaLightPattern pattern, std::stop_token stop, PathHit *first = nullptr) const
    {
        if (rectangles_.empty() || surface.type != PathSurface::Diffuse)
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

    struct Bounce
    {
        Ray ray;
        Vec3 weight;
    };

    [[nodiscard]] static Bounce specular_bounce(const Interaction &surface, Vec3 incident, Vec3 geometric, Vec3 albedo,
                                                PathSampler &sampler)
    {
        const Vec3 normal = dot(surface.normal, incident) < 0 ? surface.normal : geometric;
        if (surface.type == PathSurface::Mirror)
        {
            Vec3 direction = mirror_direction(incident, normal);
            if (dot(direction, geometric) <= 0)
            {
                direction = mirror_direction(incident, geometric);
            }
            return Bounce{surface.origin.along(direction), albedo};
        }
        const double eta = surface.back_face ? surface.ior : 1 / surface.ior;
        const double u = sampler.uniform();
        auto event = sample_dielectric(incident, normal, eta, u);
        // At silhouettes an interpolated normal can send the ray to the wrong side of the actual triangle.
        if ((event.transmitted ? -1 : 1) * dot(event.direction, geometric) <= 0)
        {
            event = sample_dielectric(incident, geometric, eta, u);
        }
        return Bounce{surface.origin.along(event.direction), Vec3{1, 1, 1} * event.radiance_weight};
    }

    [[nodiscard]] std::optional<Bounce> sample_bounce(const Interaction &surface, Vec3 incident, PathSampler &sampler,
                                                      BounceSampling method) const
    {
        const Vec3 geometric = surface.origin.geometric_normal() * (surface.back_face ? -1 : 1);
        const auto &base = surface.material.base_color;
        const Vec3 albedo{base.r, base.g, base.b};
        if (surface.type != PathSurface::Diffuse)
        {
            return specular_bounce(surface, incident, geometric, albedo, sampler);
        }
        const Vec3 diffuse = albedo * (1 - surface.material.metallic);
        if (diffuse == Vec3{} || dot(surface.normal, incident) >= 0)
        {
            return std::nullopt;
        }
        const Vec3 origin = surface.origin.along(geometric).origin;
        const bool mixed = method == BounceSampling::LightMixture && !targets_->empty();
        Vec3 direction;
        if (mixed && sampler.uniform() < 0.5)
        {
            const auto sampled = targets_->sample(origin, sampler);
            if (!sampled)
            {
                return std::nullopt;
            }
            direction = *sampled;
        }
        else
        {
            const double u = sampler.uniform();
            const double v = sampler.uniform();
            direction = cosine_hemisphere(surface.normal, u, v);
        }
        const double cosine = dot(surface.normal, direction);
        if (cosine <= 0 || dot(geometric, direction) <= 0)
        {
            return std::nullopt;
        }
        const Ray ray{origin, direction};
        if (!mixed)
        {
            return Bounce{ray, diffuse};
        }
        const double cosine_pdf = cosine / std::numbers::pi;
        const double mixture_pdf = 0.5 * (cosine_pdf + targets_->density(ray));
        return Bounce{ray, diffuse * (cosine_pdf / mixture_pdf)};
    }

    [[nodiscard]] Vec3 trace_bounces(Interaction surface, Vec3 incident, PathSampler &sampler,
                                     const PathSampling &sampling, AreaLightPattern pattern, std::stop_token stop) const
    {
        Vec3 radiance{}, throughput{1, 1, 1};
        for (std::uint32_t depth = 0; depth < sampling.max_bounces; ++depth)
        {
            if (stop.stop_requested())
            {
                throw std::runtime_error("Rendering cancelled");
            }
            const auto bounce = sample_bounce(surface, incident, sampler, sampling.method);
            if (!bounce)
            {
                break;
            }
            const auto &weight = bounce->weight;
            throughput = {throughput.x * weight.x, throughput.y * weight.y, throughput.z * weight.z};
            const Ray ray = bounce->ray;
            const auto hit = surfaces_->intersect(ray, {0, std::numeric_limits<double>::infinity()});
            if (!hit)
            {
                break;
            }
            surface = interaction(*hit, ray);
            incident = ray.direction;
            const auto &e = surface.material.emission;
            const Vec3 incoming = Vec3{e.r, e.g, e.b} + direct_light(surface, ray, sampler.next_bits(), pattern, stop);
            radiance = radiance + Vec3{throughput.x * incoming.x, throughput.y * incoming.y, throughput.z * incoming.z};
        }
        return radiance;
    }

    const GltfScene &scene_;
    std::shared_ptr<const TriangleBvh> surfaces_, blockers_;
    std::shared_ptr<const DirectionTargets> targets_;
    std::vector<std::pair<std::size_t, AreaLighting>> rectangles_;
};
} // namespace formlight
