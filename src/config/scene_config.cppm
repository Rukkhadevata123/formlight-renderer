export module formlight.scene_config;

import std;
export import formlight.camera;
export import formlight.lighting;
export import formlight.direct_lighting;

export namespace formlight
{
enum class RenderMode
{
    Raster,
    Hybrid
};
enum class LightKind
{
    None,
    Directional,
    Rectangle
};

template <class E> struct NamedValue
{
    std::string_view name;
    E value;
};

inline constexpr std::array render_modes{NamedValue{"raster", RenderMode::Raster},
                                         NamedValue{"hybrid", RenderMode::Hybrid}};
inline constexpr std::array light_kinds{NamedValue{"none", LightKind::None},
                                        NamedValue{"directional", LightKind::Directional},
                                        NamedValue{"rectangle", LightKind::Rectangle}};

constexpr auto &choices(RenderMode) noexcept
{
    return render_modes;
}
constexpr auto &choices(LightKind) noexcept
{
    return light_kinds;
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

struct SceneSettings
{
    std::filesystem::path asset;
    bool ground{};

    void fields(this auto &self, auto &&field)
    {
        field("asset", self.asset);
        field("ground", self.ground);
    }
};

struct CameraSettings
{
    bool auto_frame{true};
    double vertical_fov_degrees{45};
    Vec3 position{1.1, 0.8, 1.5}, target{}, up{0, 1, 0};

    void fields(this auto &self, auto &&field)
    {
        field("auto_frame", self.auto_frame);
        field("vertical_fov_degrees", self.vertical_fov_degrees);
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

    void fields(this auto &self, auto &&field)
    {
        field("mode", self.mode);
        field("width", self.extent.width);
        field("height", self.extent.height);
        field("light_samples", self.sampling.samples);
        field("seed", self.sampling.seed);
    }
};

struct LightSettings
{
    LightKind type{LightKind::None};
    bool shadows{true};
    Vec3 toward_light{1, 4, 3};
    Vec3 center{3, 3, 0.75};
    double width{1.5}, height{1.5}, power{240};

    void fields(this auto &self, auto &&field)
    {
        field("type", self.type);
        if (self.type != LightKind::None)
        {
            field("shadows", self.shadows);
        }
        if (self.type == LightKind::Directional)
        {
            field("toward_light", self.toward_light);
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

struct SceneConfig
{
    SceneSettings scene;
    CameraSettings camera;
    RenderSettings render;
    LightSettings light;
    DisplaySettings display;

    void fields(this auto &self, auto &&field)
    {
        field("scene", self.scene);
        field("camera", self.camera);
        field("render", self.render);
        field("light", self.light);
        field("display", self.display);
    }
};

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
};
struct AreaLighting
{
    RectangleLight light;
    LightSampling sampling{};
    bool shadows{true};
};
using PreviewLighting = std::variant<DirectionalLighting, AreaLighting>;

// A render owns a snapshot. The editor can continue changing its SceneConfig.
struct RenderRequest
{
    std::filesystem::path asset;
    bool ground;
    ImageExtent extent;
    double vertical_fov_radians;
    std::optional<PinholeCamera> camera;
    std::optional<PreviewLighting> lighting;
};

[[nodiscard]] RenderRequest prepare_render(const SceneConfig &config)
{
    const auto &[scene, camera, render, light, display] = config;
    display.validate();
    if (scene.asset.empty())
    {
        throw std::invalid_argument("scene.asset requires a model path");
    }
    if (render.extent.width <= 0 || render.extent.height <= 0 ||
        render.extent.width > std::numeric_limits<int>::max() / render.extent.height)
    {
        throw std::invalid_argument("render.width and render.height must form a positive, representable image");
    }
    if (render.sampling.samples == 0)
    {
        throw std::invalid_argument("render.light_samples must be positive");
    }
    if (!(camera.vertical_fov_degrees > 0 && camera.vertical_fov_degrees < 180))
    {
        throw std::invalid_argument("camera.vertical_fov_degrees must be between 0 and 180");
    }
    RenderRequest request{
        scene.asset, scene.ground, render.extent, camera.vertical_fov_degrees * (std::numbers::pi / 180), {}, {}};
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
    if (light.type == LightKind::Directional)
    {
        const auto method = render.mode == RenderMode::Hybrid ? ShadowMethod::Ray : ShadowMethod::Map;
        request.lighting =
            DirectionalLighting{DirectionalLight{light.toward_light}, light.shadows ? method : ShadowMethod::None};
    }
    else if (light.type == LightKind::Rectangle)
    {
        if (light.shadows && render.mode != RenderMode::Hybrid)
        {
            throw std::invalid_argument("light.shadows for a rectangle requires render.mode = 'hybrid'");
        }
        if (!(light.width > 0 && light.height > 0) || !std::isfinite(light.width) || !std::isfinite(light.height))
        {
            throw std::invalid_argument("light.width and light.height must be finite and positive");
        }
        const RectangleLight rectangle{light.center, {light.width, 0, 0}, {0, 0, light.height}, light.power};
        request.lighting = AreaLighting{rectangle, render.sampling, light.shadows};
    }
    return request;
}
} // namespace formlight
