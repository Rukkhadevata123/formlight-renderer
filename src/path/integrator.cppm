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
    SurfaceHit intersection;
    SurfaceMaterial material;
    bool back_face{};
    std::optional<Vec3> shading_normal{};
    std::vector<DiagnosticLight> lights{};
    std::optional<Vec3> bounce_radiance{};
};

// For a fixed surface only the bounce term varies. A medium instead accumulates the full free-flight sample.
export struct RayRadianceSum
{
    Vec3 rgb{};
    double transparency{};

    void add(LinearColor sample) noexcept
    {
        rgb = rgb + Vec3{sample.r, sample.g, sample.b};
        transparency += 1.0 - sample.a;
    }

    [[nodiscard]] LinearColor mean(std::uint32_t count) const noexcept
    {
        return {static_cast<float>(rgb.x / count), static_cast<float>(rgb.y / count), static_cast<float>(rgb.z / count),
                static_cast<float>(1.0 - transparency / count)};
    }
};

// The scene must outlive the integrator. Closed dielectrics expose both interfaces to path rays.
export class PathIntegrator
{
  public:
    explicit PathIntegrator(const GltfScene &scene) : scene_(scene)
    {
        if (scene.requires_preview_optics)
        {
            throw std::invalid_argument(
                "Required glTF material transmission, volume or IOR needs raster or hybrid mode");
        }
        surfaces_ = std::make_shared<SurfaceBvh>(
            scene.instances(), scene.spheres(), scene.solid_filter(),
            [&scene](SurfaceId id)
            {
                const auto &face = scene.instance(id.instance).triangles[std::to_underlying(id.primitive)];
                const auto &mat = scene.material(face);
                return mat.double_sided || mat.path_surface == PathSurface::Dielectric ? CullFace::None
                                                                                       : CullFace::Back;
            });
        targets_ = std::make_shared<DirectionTargets>(
            scene.instances(),
            [&scene](const Triangle &face)
            {
                const auto &material = scene.material(face);
                const auto &e = material.emission_multiplier;
                return material.alpha_mode != AlphaMode::Blend && (e.r > 0 || e.g > 0 || e.b > 0);
            },
            scene.spheres());
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
                    blockers_ =
                        std::make_shared<SurfaceBvh>(scene_.instances(), scene_.spheres(), scene_.solid_filter(),
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
                                       std::stop_token stop = {}, PathSampling sampling = {},
                                       std::optional<MediumHit> *medium_record = nullptr) const
    {
        RayRadianceSum sum;
        return accumulate(ray, interval, sum, 0, sampling, pixel, pattern, stop, record, medium_record);
    }

    // Append [first, sampling.samples) to the sum for this same ray and return the mean over the full prefix.
    [[nodiscard]] LinearColor accumulate(const Ray &ray, RayInterval interval, RayRadianceSum &prefix,
                                         std::uint32_t first, PathSampling sampling, std::uint64_t pixel,
                                         AreaLightPattern pattern, std::stop_token stop,
                                         std::optional<PathHit> *record = nullptr,
                                         std::optional<MediumHit> *medium_record = nullptr) const
    {
        if (first >= sampling.samples)
        {
            throw std::invalid_argument("Path sample range must be nonempty");
        }
        auto sum = prefix;
        const auto hit = surfaces_->intersect_surface(ray, interval);
        if (medium_record)
        {
            medium_record->reset();
        }
        if (scene_.media.empty())
        {
            const auto color = surface_radiance(ray, hit, record, pixel, pattern, stop, sampling, first, sum.rgb);
            prefix = sum;
            return color;
        }

        if (hit)
        {
            interval.max = hit->t;
        }
        // Each independent primary free flight owns one continuation. Diagnostics describe the first new sample.
        const auto count = sampling.samples;
        sampling.samples = 1;
        for (std::uint32_t i = first; i < count; ++i)
        {
            if (stop.stop_requested())
            {
                throw std::runtime_error("Rendering cancelled");
            }
            PathSampler rng{sampling.seed, pixel, i};
            if (const auto event = sample_free_flight(scene_.media, ray, interval, rng))
            {
                if (i == first && medium_record)
                {
                    *medium_record = event;
                }
                if (i == first && record)
                {
                    record->reset();
                }
                if (event->scattering_albedo != Vec3{})
                {
                    sum.rgb = sum.rgb + medium_direct_light(*event, ray, rng.next_bits(), pattern, stop);
                    sum.rgb = sum.rgb + trace_bounces(*event, ray, rng, sampling, pattern, stop);
                }
                continue;
            }
            Vec3 bounce_sum{};
            const auto color = surface_radiance(ray, hit, i == first ? record : nullptr, rng.next_bits(), pattern, stop,
                                                sampling, 0, bounce_sum);
            sum.add(color);
        }
        prefix = sum;
        return sum.mean(count);
    }

  private:
    [[nodiscard]] LinearColor surface_radiance(const Ray &ray, const std::optional<SurfaceHit> &hit,
                                               std::optional<PathHit> *record, std::uint64_t pixel,
                                               AreaLightPattern pattern, std::stop_token stop, PathSampling sampling,
                                               std::uint32_t first_sample, Vec3 &sum) const
    {
        if (!hit)
        {
            if (record)
            {
                record->reset();
            }
            const Vec3 e = scene_.environment;
            return {static_cast<float>(e.x), static_cast<float>(e.y), static_cast<float>(e.z), 1};
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
        for (std::uint32_t i = first_sample; i < sampling.samples; ++i)
        {
            PathSampler sampler{sampling.seed, pixel, i};
            sum = sum + trace_bounces(surface, ray, sampler, sampling, pattern, stop);
        }
        const Vec3 bounced = sum / sampling.samples;
        if (first)
        {
            first->bounce_radiance = bounced;
        }
        return surface.material.outgoing_color(direct + bounced);
    }

    struct Interaction
    {
        SurfaceMaterial material;
        SurfaceRayOrigin origin;
        Vec3 normal;
        bool back_face;
        PathSurface type;
        double ior;
        double fuzz;
    };

    [[nodiscard]] Interaction interaction(const SurfaceHit &hit, const Ray &ray) const
    {
        if (const auto *point = std::get_if<SphereCoordinates>(&hit.surface.coordinates))
        {
            const auto &sphere = scene_.spheres()[point->index];
            const auto &mat = scene_.material(sphere.material_index);
            const auto n = point->outward_normal;
            const bool back = dot(n, ray.direction) > 0;
            return {scene_.surface_material(sphere, n),
                    SurfaceRayOrigin{sphere.shape, n, ray.time},
                    back ? n * -1 : n,
                    back,
                    mat.path_surface,
                    mat.ior,
                    mat.fuzz};
        }
        const auto [id, weights, gradients] = hit.surface.triangle();
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
                SurfaceRayOrigin{instance, face, weights, ray.time},
                scene_.shading_normal(instance, face, weights, gradients, back),
                back,
                mat.path_surface,
                mat.ior,
                mat.fuzz};
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
            const Vec3 contribution = rectangle.evaluate(surface.origin, brdf, pixel, blockers_.get(), pattern,
                                                         light_record, stop, scene_.media);
            direct = direct + contribution;
            if (light_record)
            {
                light_record->scene_index = index;
                light_record->direct = contribution;
            }
        }
        return direct;
    }

    [[nodiscard]] Vec3 medium_direct_light(const MediumHit &event, const Ray &ray, std::uint64_t pixel,
                                           AreaLightPattern pattern, std::stop_token stop) const
    {
        const Vec3 position = ray.at(event.t);
        Vec3 integrated_radiance{};
        for (const auto &entry : rectangles_)
        {
            const auto &area = entry.second;
            const auto evaluate = [&](Vec3 endpoint)
            {
                const auto incident = area.light.incident(position, endpoint);
                const Ray connection{position, endpoint - position, ray.time};
                if (incident.weight == Vec3{} || (area.shadows && blockers_->occluded(connection, {0, 1})))
                {
                    return Vec3{};
                }
                return incident.weight * medium_transmittance(scene_.media, connection, {0, 1});
            };
            integrated_radiance = integrated_radiance + integrate_rectangle_samples(area.light, area.sampling, pixel,
                                                                                    pattern, stop, evaluate);
        }
        return component_product(integrated_radiance, event.scattering_albedo) / (4 * std::numbers::pi);
    }

    struct Bounce
    {
        Ray ray;
        Vec3 weight;
    };

    [[nodiscard]] static std::optional<Bounce> specular_bounce(const Interaction &surface, Vec3 incident,
                                                               Vec3 geometric, Vec3 albedo, PathSampler &sampler)
    {
        const Vec3 normal = dot(surface.normal, incident) < 0 ? surface.normal : geometric;
        if (surface.type == PathSurface::Mirror || surface.type == PathSurface::Metal)
        {
            Vec3 direction = mirror_direction(incident, normal);
            if (dot(direction, geometric) <= 0)
            {
                direction = mirror_direction(incident, geometric);
            }
            const double fuzz = surface.type == PathSurface::Metal ? surface.fuzz : 0;
            const auto scattered = sample_metal(direction, geometric, fuzz, sampler);
            if (!scattered)
            {
                return std::nullopt;
            }
            return Bounce{surface.origin.along(*scattered), albedo};
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
        const Ray outward = surface.origin.along(geometric);
        const bool mixed = method == BounceSampling::LightMixture && !targets_->empty();
        Vec3 direction{};
        if (mixed && sampler.uniform() < 0.5)
        {
            const auto sampled = targets_->sample(outward.origin, sampler, outward.time);
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
        const Ray ray{outward.origin, direction, outward.time};
        if (!mixed)
        {
            return Bounce{ray, diffuse};
        }
        const double cosine_pdf = cosine / std::numbers::pi;
        const double mixture_pdf = 0.5 * (cosine_pdf + targets_->density(ray));
        return Bounce{ray, diffuse * (cosine_pdf / mixture_pdf)};
    }

    using PathVertex = std::variant<Interaction, MediumHit>;

    [[nodiscard]] std::optional<Bounce> sample_bounce(const PathVertex &vertex, const Ray &incident,
                                                      PathSampler &sampler, BounceSampling method) const
    {
        if (const auto *surface = std::get_if<Interaction>(&vertex))
        {
            return sample_bounce(*surface, incident.direction, sampler, method);
        }
        const auto &event = std::get<MediumHit>(vertex);
        const double u = sampler.uniform();
        const Vec3 direction = uniform_sphere(u, sampler.uniform());
        // The isotropic phase function and its sampling PDF are both 1/(4*pi).
        return Bounce{{incident.at(event.t), direction, incident.time}, event.scattering_albedo};
    }

    [[nodiscard]] Vec3 trace_bounces(PathVertex vertex, Ray incident, PathSampler &sampler,
                                     const PathSampling &sampling, AreaLightPattern pattern, std::stop_token stop) const
    {
        Vec3 radiance{}, throughput{1, 1, 1};
        for (std::uint32_t depth = 0; depth < sampling.max_bounces; ++depth)
        {
            if (stop.stop_requested())
            {
                throw std::runtime_error("Rendering cancelled");
            }
            const auto bounce = sample_bounce(vertex, incident, sampler, sampling.method);
            if (!bounce)
            {
                break;
            }
            const auto &weight = bounce->weight;
            throughput = component_product(throughput, weight);
            const Ray ray = bounce->ray;
            RayInterval interval{0, std::numeric_limits<double>::infinity()};
            const auto hit = surfaces_->intersect_surface(ray, interval);
            if (!scene_.media.empty())
            {
                interval.max = hit ? hit->t : interval.max;
                if (const auto event = sample_free_flight(scene_.media, ray, interval, sampler))
                {
                    if (event->scattering_albedo == Vec3{})
                    {
                        break;
                    }
                    const auto direct = medium_direct_light(*event, ray, sampler.next_bits(), pattern, stop);
                    radiance = radiance + component_product(throughput, direct);
                    vertex = *event;
                    incident = ray;
                    continue;
                }
            }
            if (!hit)
            {
                radiance = radiance + component_product(throughput, scene_.environment);
                break;
            }
            incident = ray;
            const auto &surface = vertex.emplace<Interaction>(interaction(*hit, ray));
            const auto &e = surface.material.emission;
            const Vec3 incoming = Vec3{e.r, e.g, e.b} + direct_light(surface, ray, sampler.next_bits(), pattern, stop);
            radiance = radiance + component_product(throughput, incoming);
        }
        return radiance;
    }

    const GltfScene &scene_;
    std::shared_ptr<const SurfaceBvh> surfaces_, blockers_;
    std::shared_ptr<const DirectionTargets> targets_;
    std::vector<std::pair<std::size_t, AreaLighting>> rectangles_;
};
} // namespace formlight
