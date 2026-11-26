export module formlight.preview;

import std;
export import formlight.gltf;
export import formlight.scene_config;
export import formlight.raster;

namespace formlight
{
export struct PreviewView
{
    PinholeCamera camera;
    ViewDepthRange depth{};
};

export void add_preview_ground(GltfScene &scene, const Bounds3 &subject)
{
    const auto center = subject.center();
    const double half = 3 * subject.radius(), y = subject.low.y;
    const TriangleVertex a{{center.x - half, y, center.z - half}}, b{{center.x - half, y, center.z + half}},
        c{{center.x + half, y, center.z + half}}, d{{center.x + half, y, center.z - half}};
    scene.add_solid_mesh({Triangle{.vertices = {a, b, c}}, Triangle{.vertices = {a, c, d}}}, {0.4f, 0.4f, 0.4f, 1});
}

PreviewView clip_to_bounds(const PinholeCamera &camera, const Bounds3 &bounds, double minimum_near)
{
    const double center = camera.to_view(bounds.center()).depth;
    const double radius = bounds.radius();
    const double near_depth = std::max(minimum_near, center - radius);
    return {camera, {near_depth, std::max(center + radius, 2 * minimum_near)}};
}

// Frame the subject, but include all scene geometry in the camera's depth range.
export [[nodiscard]] PreviewView frame_scene(const GltfScene &scene, ImageExtent extent,
                                             std::optional<Bounds3> subject = {},
                                             double fov = CameraSettings{}.vertical_fov_degrees *
                                                          (std::numbers::pi / 180))
{
    const auto bounds = world_bounds(scene.instances());
    const auto framing = subject.value_or(bounds);
    const auto center = framing.center();
    const double radius = framing.radius();
    const double half_angle =
        std::atan(std::tan(fov / 2) * std::min(1.0, static_cast<double>(extent.width) / extent.height));
    const double distance = 1.05 * radius / std::sin(half_angle);
    constexpr CameraSettings defaults;
    const Vec3 direction = unit_vector(defaults.position - defaults.target, "Invalid default camera direction");
    const PinholeCamera camera{{center + direction * distance, center, defaults.up, fov}, extent};
    return clip_to_bounds(camera, bounds, distance * 1e-5);
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

export [[nodiscard]] ColorImage render_preview(const GltfScene &scene, const PreviewView &view,
                                               std::optional<PreviewLighting> lighting = std::nullopt);
export [[nodiscard]] PixelDiagnostic diagnose_pixel(const GltfScene &scene, const PreviewView &view, PixelCoord pixel,
                                                    std::optional<PreviewLighting> lighting = std::nullopt);

struct ScenePreview
{
    GltfScene scene;
    PreviewView view;
};

ScenePreview load_preview(const RenderRequest &request)
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
    const auto view = [&]
    {
        if (request.camera)
        {
            const auto bounds = world_bounds(scene->instances());
            return clip_to_bounds(*request.camera, bounds, bounds.radius() * 1e-5);
        }
        return frame_scene(*scene, request.extent, subject, request.vertical_fov_radians);
    }();
    return {std::move(*scene), view};
}

export [[nodiscard]] ColorImage render_scene(const RenderRequest &request)
{
    const auto [scene, view] = load_preview(request);
    return render_preview(scene, view, request.lighting);
}

export [[nodiscard]] PixelDiagnostic diagnose_pixel(const RenderRequest &request, PixelCoord pixel)
{
    const auto [scene, view] = load_preview(request);
    return diagnose_pixel(scene, view, pixel, request.lighting);
}
} // namespace formlight
