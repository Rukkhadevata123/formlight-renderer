export module formlight.preview;

import std;
export import formlight.gltf;
export import formlight.scene_config;
export import formlight.raster;
import formlight.ray_query;
import formlight.shadow_map;
import formlight.path;

namespace formlight
{
export struct PreviewView
{
    PinholeCamera camera;
    ViewDepthRange depth{};
    // Pixel inspection replays the light samples used by the displayed frame.
    AreaLightPattern area_light_pattern{AreaLightPattern::ShiftedHalton};
    int ssaa{1};
    PathSampling path_sampling{};
};

export void add_preview_ground(GltfScene &scene, const Bounds3 &subject)
{
    const auto center = subject.center();
    const double radius = subject.radius();
    const double half = 3 * radius;
    // Separate the floor from coplanar bottom faces, even if the gap rounds away at large world coordinates.
    const double y = std::nextafter(subject.low.y - radius * 1e-7, -std::numeric_limits<double>::infinity());
    const TriangleVertex a{{center.x - half, y, center.z - half}}, b{{center.x - half, y, center.z + half}},
        c{{center.x + half, y, center.z + half}}, d{{center.x + half, y, center.z - half}};
    scene.add_solid_mesh({Triangle{.vertices = {a, b, c}}, Triangle{.vertices = {a, c, d}}}, {0.4f, 0.4f, 0.4f, 1});
}

export PreviewView clip_to_bounds(const PinholeCamera &camera, const Bounds3 &bounds)
{
    const double center = camera.to_view(bounds.center()).depth;
    const double radius = bounds.radius();
    // A saved pose must clip identically when reopened, including close-ups inside the scene bounds.
    const auto pose = camera.view();
    const Vec3 offset = pose.position - pose.target;
    const double minimum_near = 1e-5 * std::min(radius, std::hypot(offset.x, offset.y, offset.z));
    const double near_depth = std::max(minimum_near, center - radius);
    return {camera, {near_depth, std::max(center + radius, 2 * minimum_near)}};
}

export [[nodiscard]] double framing_distance(double radius, ImageExtent extent, double fov)
{
    const double half_angle =
        std::atan(std::tan(fov / 2) * std::min(1.0, static_cast<double>(extent.width) / extent.height));
    return 1.05 * radius / std::sin(half_angle);
}

// Frame the subject, but include all scene geometry in the camera's depth range.
export [[nodiscard]] PreviewView frame_bounds(Bounds3 bounds, Bounds3 subject, ImageExtent extent, double fov)
{
    const auto center = subject.center();
    const double distance = framing_distance(subject.radius(), extent, fov);
    constexpr CameraSettings defaults;
    const Vec3 direction = unit_vector(defaults.position - defaults.target, "Invalid default camera direction");
    const PinholeCamera camera{{center + direction * distance, center, defaults.up, fov}, extent};
    return clip_to_bounds(camera, bounds);
}

export [[nodiscard]] PreviewView frame_scene(const GltfScene &scene, ImageExtent extent,
                                             std::optional<Bounds3> subject = {},
                                             double fov = CameraSettings{}.vertical_fov_degrees *
                                                          (std::numbers::pi / 180))
{
    const auto bounds = world_bounds(scene.instances());
    return frame_bounds(bounds, subject.value_or(bounds), extent, fov);
}

export struct DiagnosticSurface
{
    RasterHit hit;
    std::size_t material_index;
    SurfaceMaterial material;
    Vec3 ambient;
    std::optional<Vec3> shading_normal{};
};

export struct SampleDiagnostic
{
    ImagePoint position; // Coordinates in the output image.
    double weight;
    std::optional<DiagnosticSurface> surface{};
    std::vector<DiagnosticLight> lights{};
    LinearColor color{};
    std::optional<Vec3> bounce_radiance{};

    // Selected surface's contribution to the final pixel, including opacity and the sample's share of the mean.
    [[nodiscard]] Vec3 sample_contribution(std::size_t light, std::size_t index) const
    {
        const auto &samples = lights.at(light).samples;
        const auto &sample = samples.at(index);
        return sample.direct * (weight * surface.value().material.base_color.a / static_cast<double>(samples.size()));
    }
};

export struct PixelDiagnostic
{
    PixelCoord pixel;
    std::vector<SampleDiagnostic> samples;
    LinearColor color;
};

// Borrows a static scene. Camera changes and pixel inspections share the prepared shadow data.
export class PreviewRenderer
{
  public:
    explicit PreviewRenderer(const GltfScene &scene, std::optional<PreviewLighting> lighting = {},
                             RenderMode mode = RenderMode::Raster);
    // Copies share immutable shadow data. Relighting one copy leaves in-flight frames and picks intact.
    void set_lighting(std::optional<PreviewLighting> lighting);
    void configure(RenderMode mode, std::optional<PreviewLighting> lighting);
    [[nodiscard]] ColorImage render(const PreviewView &view, std::stop_token stop = {}) const;
    [[nodiscard]] PixelDiagnostic diagnose(const PreviewView &view, PixelCoord pixel, std::stop_token stop = {}) const;

  private:
    [[nodiscard]] ColorImage render_sample_grid(const PreviewView &grid, std::stop_token stop) const;
    [[nodiscard]] LinearColor trace_primary(const PreviewView &grid, PixelCoord pixel, std::stop_token stop,
                                            SampleDiagnostic *record = nullptr) const;
    void diagnose_sample(const PreviewView &grid, PixelCoord sample, std::span<const RasterTriangle> pieces,
                         SampleDiagnostic &report, std::stop_token stop) const;
    [[nodiscard]] LinearColor shade(const RasterHit &hit, PixelCoord pixel, const PreviewView &view,
                                    const std::stop_token &stop, SampleDiagnostic *record = nullptr) const;
    [[nodiscard]] Vec3 shade_light(std::size_t index, const MetallicRoughnessBrdf &brdf, const MeshInstance &instance,
                                   const Triangle &face, Barycentrics weights, std::uint64_t pixel,
                                   AreaLightPattern pattern, std::stop_token stop, DiagnosticLight *record) const;
    [[nodiscard]] ShadowResult directional_shadow(const DirectionalLighting &light, const DirectionalShadowMap *map,
                                                  const MeshInstance &instance, const Triangle &face,
                                                  Barycentrics weights, bool identify_blocker) const;
    const GltfScene &scene_;
    SurfaceFilter solid_filter_;
    std::optional<PreviewLighting> lighting_;
    std::vector<std::shared_ptr<const DirectionalShadowMap>> shadow_maps_;
    std::shared_ptr<const TriangleBvh> blockers_;
    std::optional<PathIntegrator> path_;
};

export [[nodiscard]] ColorImage render_preview(const GltfScene &scene, const PreviewView &view,
                                               std::optional<PreviewLighting> lighting = std::nullopt,
                                               std::stop_token stop = {})
{
    return PreviewRenderer{scene, std::move(lighting)}.render(view, stop);
}
export [[nodiscard]] PixelDiagnostic diagnose_pixel(const GltfScene &scene, const PreviewView &view, PixelCoord pixel,
                                                    std::optional<PreviewLighting> lighting = std::nullopt,
                                                    std::stop_token stop = {})
{
    return PreviewRenderer{scene, std::move(lighting)}.diagnose(view, pixel, stop);
}

export struct ScenePreview
{
    GltfScene scene;
    PreviewView view;
    Bounds3 bounds;
    Bounds3 subject;
};

export [[nodiscard]] ScenePreview load_preview(const RenderRequest &request)
{
    auto scene = GltfScene::load(request.asset, GltfContent::Materials);
    if (!scene)
    {
        throw std::runtime_error(scene.error());
    }
    const auto subject = world_bounds(scene->instances());
    auto bounds = subject;
    if (request.ground)
    {
        add_preview_ground(*scene, subject);
        bounds = world_bounds(scene->instances());
    }
    auto view = request.camera ? clip_to_bounds(*request.camera, bounds)
                               : frame_bounds(bounds, subject, request.extent, request.vertical_fov_radians);
    view.ssaa = request.ssaa;
    view.path_sampling = request.path_sampling;
    return {std::move(*scene), view, bounds, subject};
}

export [[nodiscard]] ColorImage render_scene(const RenderRequest &request)
{
    const auto preview = load_preview(request);
    return PreviewRenderer{preview.scene, request.lighting, request.mode}.render(preview.view);
}

export [[nodiscard]] PixelDiagnostic diagnose_pixel(const RenderRequest &request, PixelCoord pixel)
{
    const auto preview = load_preview(request);
    return PreviewRenderer{preview.scene, request.lighting, request.mode}.diagnose(preview.view, pixel);
}
} // namespace formlight
