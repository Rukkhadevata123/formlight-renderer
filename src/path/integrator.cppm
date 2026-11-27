export module formlight.path;

import std;
import formlight.gltf;
import formlight.ray_query;
import formlight.scene_config;

namespace formlight
{
export struct PathHit
{
    RayHit intersection;
    SurfaceMaterial material;
    bool back_face{};
    std::optional<Vec3> shading_normal{};
    std::vector<DiagnosticLight> lights{};
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
                                       std::stop_token stop = {}) const
    {
        const auto hit = surfaces_->intersect(ray, interval);
        if (!hit)
        {
            if (record)
            {
                record->reset();
            }
            return {}; // Black environment.
        }
        const auto &[id, weights, gradients] = hit->surface;
        const auto &instance = scene_.instance(id.instance);
        const auto &face = instance.triangles[std::to_underlying(id.primitive)];
        const auto material = scene_.surface_material(face, weights, gradients);
        const bool back_face = dot(face.geometric_normal(), instance.transform.to_local_vector(ray.direction)) > 0;
        PathHit *first = record ? &record->emplace(*hit, material, back_face) : nullptr;
        if (rectangles_.empty())
        {
            return material.emission;
        }
        const Vec3 normal = scene_.shading_normal(instance, face, weights, gradients, back_face);
        const MetallicRoughnessBrdf brdf{material, normal, ray.direction * -1};
        const SurfaceRayOrigin receiver{instance, face, weights};
        if (first)
        {
            first->shading_normal = normal;
        }
        Vec3 direct{};
        for (const auto &[index, rectangle] : rectangles_)
        {
            auto *light_record = first ? &first->lights.emplace_back() : nullptr;
            const Vec3 contribution =
                rectangle.evaluate(receiver, brdf, pixel, blockers_.get(), pattern, light_record, stop);
            direct = direct + contribution;
            if (light_record)
            {
                light_record->scene_index = index;
                light_record->direct = contribution;
            }
        }
        return material.outgoing_color(direct);
    }

  private:
    const GltfScene &scene_;
    std::shared_ptr<const TriangleBvh> surfaces_, blockers_;
    std::vector<std::pair<std::size_t, AreaLighting>> rectangles_;
};
} // namespace formlight
