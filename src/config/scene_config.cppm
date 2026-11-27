export module formlight.scene_config;

import std;
export import formlight.camera;
export import formlight.specular;
export import formlight.sphere;
export import formlight.lighting;
export import formlight.direct_lighting;

export namespace formlight
{
enum class RenderMode
{
    Raster,
    Hybrid,
    Path
};

enum class ScenePreset
{
    None,
    ManyBalls
};

enum class BounceSampling
{
    Cosine,
    LightMixture
};

template <class E> struct NamedValue
{
    std::string_view name;
    E value;
};

inline constexpr std::array render_modes{NamedValue{"raster", RenderMode::Raster},
                                         NamedValue{"hybrid", RenderMode::Hybrid},
                                         NamedValue{"path", RenderMode::Path}};
inline constexpr std::array light_kinds{NamedValue{"directional", LightKind::Directional},
                                        NamedValue{"point", LightKind::Point},
                                        NamedValue{"rectangle", LightKind::Rectangle}};
inline constexpr std::array tone_mappings{NamedValue{"clip", ToneMapping::Clip},
                                          NamedValue{"reinhard", ToneMapping::Reinhard}};

constexpr auto &choices(RenderMode) noexcept
{
    return render_modes;
}
inline constexpr std::array scene_presets{NamedValue{"none", ScenePreset::None},
                                          NamedValue{"many_balls", ScenePreset::ManyBalls}};
constexpr auto &choices(ScenePreset) noexcept
{
    return scene_presets;
}
inline constexpr std::array path_surfaces{
    NamedValue{"diffuse", PathSurface::Diffuse}, NamedValue{"mirror", PathSurface::Mirror},
    NamedValue{"dielectric", PathSurface::Dielectric}, NamedValue{"metal", PathSurface::Metal}};
constexpr auto &choices(PathSurface) noexcept
{
    return path_surfaces;
}
inline constexpr std::array bounce_sampling_modes{NamedValue{"cosine", BounceSampling::Cosine},
                                                  NamedValue{"light_mixture", BounceSampling::LightMixture}};
constexpr auto &choices(BounceSampling) noexcept
{
    return bounce_sampling_modes;
}
constexpr auto &choices(LightKind) noexcept
{
    return light_kinds;
}
constexpr auto &choices(ToneMapping) noexcept
{
    return tone_mappings;
}

template <class E> E parse_choice(std::string_view name)
{
    for (const auto &choice : choices(E{}))
    {
        if (choice.name == name)
        {
            return choice.value;
        }
    }
    throw std::invalid_argument(std::format("Unknown value '{}'", name));
}

// Each visitor receives the persisted field name and a reference into the editable configuration.
template <class T>
concept ConfigGroup = requires(T &group) { group.fields([](auto &&...) {}); };

struct PathMaterialSettings
{
    int material{-1}; // Zero-based glTF material index.
    PathSurface type{PathSurface::Diffuse};
    double ior{1.5};
    double fuzz{};

    void fields(this auto &self, auto &&field)
    {
        field("material", self.material);
        field("type", self.type);
        if (self.type == PathSurface::Dielectric)
        {
            field("ior", self.ior);
        }
        if (self.type == PathSurface::Metal)
        {
            field("fuzz", self.fuzz);
        }
    }
};

struct SphereSettings
{
    Sphere shape;
    Vec3 albedo{1, 1, 1};
    PathSurface type{PathSurface::Diffuse};
    double ior{1.5};
    bool sampling_target{};
    double fuzz{};

    void fields(this auto &self, auto &&field)
    {
        field("center", self.shape.center);
        field("radius", self.shape.radius);
        field("type", self.type);
        if (self.type == PathSurface::Dielectric)
        {
            field("ior", self.ior);
        }
        else
        {
            field("albedo", self.albedo);
        }
        if (self.type == PathSurface::Metal)
        {
            field("fuzz", self.fuzz);
        }
        field("sampling_target", self.sampling_target);
    }
};

struct SceneSettings
{
    std::filesystem::path asset;
    bool ground{};
    std::vector<PathMaterialSettings> path_materials{};
    std::vector<std::uint32_t> sampling_targets{}; // glTF node indices in the active scene.
    std::vector<SphereSettings> spheres{};
    ScenePreset preset{ScenePreset::None};
    std::uint64_t seed{42};
    Vec3 environment{};
    double metal_fuzz_max{0.5};

    void fields(this auto &self, auto &&field)
    {
        field("asset", self.asset);
        field("ground", self.ground);
        field("path_materials", self.path_materials);
        field("sampling_targets", self.sampling_targets);
        field("spheres", self.spheres);
        field("preset", self.preset);
        field("environment", self.environment);
        if (self.preset != ScenePreset::None)
        {
            field("seed", self.seed);
            field("metal_fuzz_max", self.metal_fuzz_max);
        }
    }
};

struct LensSettings
{
    double focus_distance{1}, defocus_angle_degrees{};

    void fields(this auto &self, auto &&field)
    {
        field("focus_distance", self.focus_distance);
        field("defocus_angle_degrees", self.defocus_angle_degrees);
    }
};

struct CameraSettings
{
    bool auto_frame{true};
    double vertical_fov_degrees{45};
    Vec3 position{1.1, 0.8, 1.5}, target{}, up{0, 1, 0};
    LensSettings lens{};

    void fields(this auto &self, auto &&field)
    {
        field("auto_frame", self.auto_frame);
        field("vertical_fov_degrees", self.vertical_fov_degrees);
        self.lens.fields(field);
        if (!self.auto_frame)
        {
            field("position", self.position);
            field("target", self.target);
            field("up", self.up);
        }
    }
};

struct RenderSettings
{
    RenderMode mode{RenderMode::Raster};
    ImageExtent extent{640, 480};
    LightSampling sampling{};
    int ssaa{1}; // Samples per image axis.
    std::uint32_t path_samples{1}, max_bounces{};
    BounceSampling bounce_sampling{BounceSampling::Cosine};

    void fields(this auto &self, auto &&field)
    {
        field("mode", self.mode);
        field("width", self.extent.width);
        field("height", self.extent.height);
        field("ssaa", self.ssaa);
        field("light_samples", self.sampling.samples);
        field("seed", self.sampling.seed);
        field("path_samples", self.path_samples);
        field("max_bounces", self.max_bounces);
        field("bounce_sampling", self.bounce_sampling);
    }
};

struct LightSettings
{
    LightKind type{LightKind::Directional};
    bool enabled{true};
    bool shadows{true};
    bool pcf{};
    Vec3 toward_light{1, 4, 3};
    Vec3 position{3, 3, 0.75}, attenuation{PointLight::default_attenuation};
    Vec3 color{1, 1, 1};
    double intensity{DirectionalLight::default_intensity};
    Vec3 center{3, 3, 0.75};
    double width{1.5}, height{1.5}, power{240};

    void fields(this auto &self, auto &&field)
    {
        field("type", self.type);
        field("enabled", self.enabled);
        field("shadows", self.shadows);
        if (self.type == LightKind::Directional || self.type == LightKind::Point)
        {
            field("color", self.color);
            field("intensity", self.intensity);
        }
        if (self.type == LightKind::Directional)
        {
            field("toward_light", self.toward_light);
            field("pcf", self.pcf);
        }
        else if (self.type == LightKind::Point)
        {
            field("position", self.position);
            field("attenuation", self.attenuation);
        }
        else if (self.type == LightKind::Rectangle)
        {
            field("center", self.center);
            field("width", self.width);
            field("height", self.height);
            field("power", self.power);
        }
    }
};

struct LightingSettings
{
    Vec3 ambient{};
    std::vector<LightSettings> lights{};

    void fields(this auto &self, auto &&field)
    {
        field("ambient", self.ambient);
        field("lights", self.lights);
    }
};

struct SceneConfig
{
    SceneSettings scene;
    CameraSettings camera;
    RenderSettings render;
    std::optional<LightingSettings> lighting;
    DisplaySettings display;

    void fields(this auto &self, auto &&field)
    {
        field("scene", self.scene);
        field("camera", self.camera);
        field("render", self.render);
        field("lighting", self.lighting);
        field("display", self.display);
    }
};

[[nodiscard]] SceneConfig preset_config(ScenePreset preset);
void append_many_balls(std::vector<SphereSettings> &spheres, std::uint64_t seed, double max_fuzz = 0.5);

enum class ShadowMethod
{
    None,
    Map,
    Ray
};
struct DirectionalLighting
{
    DirectionalLight light{LightSettings{}.toward_light};
    ShadowMethod shadows{ShadowMethod::None};
    bool pcf{};
};
struct PointLighting
{
    PointLight light;
    bool shadows{true};
};
using DirectLight = std::variant<DirectionalLighting, PointLighting, AreaLighting>;

struct ActiveLight
{
    std::size_t scene_index;
    DirectLight source;
};

struct PreviewLighting
{
    std::vector<ActiveLight> direct;
    Vec3 ambient{};

    PreviewLighting() = default;

    PreviewLighting(std::convertible_to<DirectLight> auto light, Vec3 ambient_rgb = {})
        : direct{{0, DirectLight{std::move(light)}}}, ambient{ambient_rgb}
    {
    }
};

struct PathSampling
{
    std::uint32_t samples{1}, max_bounces{};
    std::uint64_t seed{};
    BounceSampling method{BounceSampling::Cosine};
};

// A render owns a snapshot. The editor can continue changing its SceneConfig.
struct RenderRequest
{
    std::filesystem::path asset;
    bool ground;
    ImageExtent extent;
    double vertical_fov_radians;
    std::optional<PinholeCamera> camera;
    std::optional<PreviewLighting> lighting;
    int ssaa{1};
    RenderMode mode{RenderMode::Raster};
    PathSampling path_sampling{};
    std::vector<PathMaterialSettings> path_materials{};
    std::vector<std::uint32_t> sampling_targets{};
    std::vector<SphereSettings> spheres{};
    Vec3 environment{};
    ThinLens lens{};
};

[[nodiscard]] ImageExtent sample_grid_extent(ImageExtent extent, int ssaa)
{
    if (ssaa != 1 && ssaa != 2)
    {
        throw std::invalid_argument("render.ssaa must be 1 or 2 samples per axis");
    }
    if (extent.width <= 0 || extent.height <= 0 ||
        extent.width > std::numeric_limits<int>::max() / extent.height / ssaa / ssaa)
    {
        throw std::invalid_argument(
            "render.width, render.height and render.ssaa must form a representable sample grid");
    }
    return {extent.width * ssaa, extent.height * ssaa};
}

[[nodiscard]] DirectLight prepare_light(const LightSettings &light, const RenderSettings &render)
{
    if (light.type == LightKind::Directional)
    {
        const auto method = render.mode == RenderMode::Raster ? ShadowMethod::Map : ShadowMethod::Ray;
        const DirectionalLight directional{light.toward_light, light.color, light.intensity};
        return DirectionalLighting{directional, light.shadows ? method : ShadowMethod::None, light.pcf};
    }
    else if (light.type == LightKind::Point)
    {
        if (light.shadows && render.mode == RenderMode::Raster)
        {
            throw std::invalid_argument("A point light with shadows requires hybrid or path mode");
        }
        const PointLight point{light.position, light.color, light.intensity, light.attenuation};
        return PointLighting{point, light.shadows};
    }

    if (light.shadows && render.mode == RenderMode::Raster)
    {
        throw std::invalid_argument("A rectangle light with shadows requires hybrid or path mode");
    }
    if (!(light.width > 0 && light.height > 0) || !std::isfinite(light.width) || !std::isfinite(light.height))
    {
        throw std::invalid_argument("light.width and light.height must be finite and positive");
    }
    const RectangleLight rectangle{light.center, {light.width, 0, 0}, {0, 0, light.height}, light.power};
    return AreaLighting{rectangle, render.sampling, light.shadows};
}

[[nodiscard]] RenderRequest prepare_render(const SceneConfig &config)
{
    const auto &[scene, camera, render, lighting, display] = config;
    display.validate();
    if (!finite_nonnegative_components(scene.environment))
    {
        throw std::invalid_argument("scene.environment requires three finite, nonnegative channels");
    }
    if (scene.asset.empty() && scene.spheres.empty() && scene.preset == ScenePreset::None)
    {
        throw std::invalid_argument("Scene requires a model path or a sphere");
    }
    if ((!scene.spheres.empty() || scene.preset == ScenePreset::ManyBalls) && render.mode != RenderMode::Path)
    {
        throw std::invalid_argument("Analytic spheres require path mode");
    }
    const auto validate_fuzz = [](double fuzz)
    {
        if (!(fuzz >= 0 && fuzz <= 1))
        {
            throw std::invalid_argument("Metal fuzz must be in [0, 1]");
        }
    };
    for (const auto &sphere : scene.spheres)
    {
        (void)sphere.shape.bounds();
        if (sphere.type == PathSurface::Metal)
        {
            validate_fuzz(sphere.fuzz);
        }
        if (sphere.type == PathSurface::Dielectric && !(std::isfinite(sphere.ior) && sphere.ior >= 1))
        {
            throw std::invalid_argument("Dielectric ior must be finite and at least 1");
        }
        for (const double channel : {sphere.albedo.x, sphere.albedo.y, sphere.albedo.z})
        {
            if (!(channel >= 0 && channel <= 1))
            {
                throw std::invalid_argument("Sphere albedo must be in [0, 1]");
            }
        }
    }
    (void)sample_grid_extent(render.extent, render.ssaa);
    std::set<int> assigned;
    for (const auto &entry : scene.path_materials)
    {
        if (entry.type == PathSurface::Metal)
        {
            validate_fuzz(entry.fuzz);
        }
        if (entry.material < 0 || !assigned.insert(entry.material).second)
        {
            throw std::invalid_argument("scene.path_materials requires distinct nonnegative glTF material indices");
        }
        if (entry.type == PathSurface::Dielectric && !(std::isfinite(entry.ior) && entry.ior >= 1))
        {
            throw std::invalid_argument("Dielectric ior must be finite and at least 1");
        }
    }
    if (render.sampling.samples == 0)
    {
        throw std::invalid_argument("render.light_samples must be positive");
    }
    if (render.path_samples == 0)
    {
        throw std::invalid_argument("render.path_samples must be positive");
    }
    if (!(camera.vertical_fov_degrees > 0 && camera.vertical_fov_degrees < 180))
    {
        throw std::invalid_argument("camera.vertical_fov_degrees must be between 0 and 180");
    }
    RenderRequest request{scene.asset,
                          scene.ground,
                          render.extent,
                          camera.vertical_fov_degrees * (std::numbers::pi / 180),
                          {},
                          {},
                          render.ssaa,
                          render.mode,
                          {render.path_samples, render.max_bounces, render.sampling.seed, render.bounce_sampling},
                          scene.path_materials,
                          scene.sampling_targets,
                          scene.spheres,
                          scene.environment,
                          ThinLens{camera.lens.focus_distance, camera.lens.defocus_angle_degrees}};
    if (request.lens.radius() > 0 && render.mode != RenderMode::Path)
    {
        throw std::invalid_argument("Camera defocus requires path mode");
    }
    if (scene.preset == ScenePreset::ManyBalls)
    {
        validate_fuzz(scene.metal_fuzz_max);
        append_many_balls(request.spheres, scene.seed, scene.metal_fuzz_max);
    }
    if (!camera.auto_frame)
    {
        try
        {
            request.camera.emplace(CameraView{camera.position, camera.target, camera.up, request.vertical_fov_radians},
                                   render.extent);
        }
        catch (const std::invalid_argument &error)
        {
            throw std::invalid_argument(std::format("camera: {}", error.what()));
        }
    }
    if (lighting)
    {
        if (!finite_nonnegative_components(lighting->ambient))
        {
            throw std::invalid_argument("lighting.ambient requires three finite, nonnegative channels");
        }
        auto &prepared = request.lighting.emplace();
        prepared.ambient = lighting->ambient;
        prepared.direct.reserve(lighting->lights.size());
        for (std::size_t i = 0; i < lighting->lights.size(); ++i)
        {
            const auto &light = lighting->lights[i];
            if (!light.enabled)
            {
                continue;
            }
            try
            {
                prepared.direct.push_back({i, prepare_light(light, render)});
            }
            catch (const std::invalid_argument &error)
            {
                throw std::invalid_argument(std::format("lighting.lights[{}]: {}", i, error.what()));
            }
        }
    }
    return request;
}
} // namespace formlight
