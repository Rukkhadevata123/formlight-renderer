export module formlight.preview;

import std;
export import formlight.gltf;
export import formlight.scene_config;
export import formlight.raster;
import formlight.ray_query;
import formlight.shadow_map;

namespace formlight
{
export struct PreviewView
{
    PinholeCamera camera;
    ViewDepthRange depth{};
    // Pixel inspection replays the light samples used by the displayed frame.
    AreaLightPattern area_light_pattern{AreaLightPattern::ShiftedHalton};
};

export void add_preview_ground(GltfScene &scene, const Bounds3 &subject)
{
    const auto center = subject.center();
    const double half = 3 * subject.radius(), y = subject.low.y;
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
PreviewView frame_bounds(Bounds3 bounds, Bounds3 subject, ImageExtent extent, double fov)
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
    LinearColor base_color;
};

export struct PixelDiagnostic
{
    PixelCoord pixel;
    std::optional<DiagnosticSurface> surface{};
    LightKind light{LightKind::None};
    std::uint64_t sampling_seed{};
    std::vector<LightSample> samples{};
    LinearColor color{};

    // Linear RGB contribution to the final pixel, including the sample's share of the mean.
    [[nodiscard]] Vec3 sample_contribution(std::size_t index) const
    {
        const auto &sample = samples.at(index);
        const auto &base = surface.value().base_color;
        return Vec3{base.r, base.g, base.b} * (sample.visible_weight() / static_cast<double>(samples.size()));
    }
};

// Borrows a static scene. Camera changes and pixel inspections share the prepared shadow data.
export class PreviewRenderer
{
  public:
    explicit PreviewRenderer(const GltfScene &scene, std::optional<PreviewLighting> lighting = {});
    // Copies share immutable shadow data. Relighting one copy leaves in-flight frames and picks intact.
    void set_lighting(std::optional<PreviewLighting> lighting);
    [[nodiscard]] ColorImage render(const PreviewView &view, std::stop_token stop = {}) const;
    [[nodiscard]] PixelDiagnostic diagnose(const PreviewView &view, PixelCoord pixel, std::stop_token stop = {}) const;

  private:
    [[nodiscard]] LinearColor shade(const RasterHit &hit, std::uint64_t pixel, AreaLightPattern area_light_pattern,
                                    const std::stop_token &stop, PixelDiagnostic *record = nullptr) const;
    [[nodiscard]] LightSample directional_sample(const DirectionalLighting &light, const MeshInstance &instance,
                                                 const Triangle &face, Barycentrics weights, Vec3 normal,
                                                 bool identify_blocker) const;
    const GltfScene &scene_;
    std::optional<PreviewLighting> lighting_;
    std::shared_ptr<const DirectionalShadowMap> shadow_map_;
    std::shared_ptr<const TriangleBvh> blockers_;
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
};

export [[nodiscard]] ScenePreview load_preview(const RenderRequest &request)
{
    auto scene = GltfScene::load_glb(request.asset, GltfContent::BaseColor);
    if (!scene)
    {
        throw std::runtime_error(scene.error());
    }
    std::optional<Bounds3> subject;
    if (request.ground)
    {
        subject = world_bounds(scene->instances());
        add_preview_ground(*scene, *subject);
    }
    const auto bounds = world_bounds(scene->instances());
    const auto view =
        request.camera ? clip_to_bounds(*request.camera, bounds)
                       : frame_bounds(bounds, subject.value_or(bounds), request.extent, request.vertical_fov_radians);
    return {std::move(*scene), view, bounds};
}

export [[nodiscard]] ColorImage render_scene(const RenderRequest &request)
{
    const auto preview = load_preview(request);
    return render_preview(preview.scene, preview.view, request.lighting);
}

export [[nodiscard]] PixelDiagnostic diagnose_pixel(const RenderRequest &request, PixelCoord pixel)
{
    const auto preview = load_preview(request);
    return diagnose_pixel(preview.scene, preview.view, pixel, request.lighting);
}
} // namespace formlight
