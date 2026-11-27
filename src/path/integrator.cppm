export module formlight.path;

import std;
import formlight.gltf;
import formlight.ray_query;

namespace formlight
{
export struct PathHit
{
    RayHit intersection;
    SurfaceMaterial material;
};

// The scene must outlive the integrator. Primary rays see the authored front of single-sided materials.
export class PathIntegrator
{
  public:
    explicit PathIntegrator(const GltfScene &scene)
        : scene_(scene), surfaces_(scene.instances(), scene.solid_filter(), scene.face_culling(CullFace::Back))
    {
    }

    [[nodiscard]] LinearColor radiance(const Ray &ray, RayInterval interval,
                                       std::optional<PathHit> *record = nullptr) const
    {
        const auto hit = surfaces_.intersect(ray, interval);
        if (!hit)
        {
            if (record)
            {
                record->reset();
            }
            return {}; // Black environment.
        }
        const auto &[id, weights, gradients] = hit->surface;
        const auto &face = scene_.instance(id.instance).triangles[std::to_underlying(id.primitive)];
        const auto material = scene_.surface_material(face, weights, gradients);
        if (record)
        {
            record->emplace(*hit, material);
        }
        return material.emission;
    }

  private:
    const GltfScene &scene_;
    TriangleBvh surfaces_;
};
} // namespace formlight
