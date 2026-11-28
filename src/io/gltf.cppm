export module formlight.gltf;

import std;
export import formlight.mesh_instance;
export import formlight.sphere;
export import formlight.texture;
export import formlight.material;
import formlight.specular;
import formlight.scene_config;

namespace formlight
{
export enum class GltfContent
{
    Geometry,
    Materials
};

export struct UvTransform
{
    TexCoord u_axis{1, 0}, v_axis{0, 1}, offset{};

    [[nodiscard]] TexCoord vector(TexCoord uv) const noexcept
    {
        return {u_axis.u * uv.u + v_axis.u * uv.v, u_axis.v * uv.u + v_axis.v * uv.v};
    }

    [[nodiscard]] TexCoord point(TexCoord uv) const noexcept
    {
        const auto rotated_scaled = vector(uv);
        return {rotated_scaled.u + offset.u, rotated_scaled.v + offset.v};
    }
};

struct TextureBinding
{
    std::size_t image;
    TextureSampler sampler;
    std::size_t texcoord_set{};
    std::optional<UvTransform> transform{};
};

export enum class AlphaMode
{
    Opaque,
    Mask,
    Blend
};

export struct GltfMaterial
{
    LinearColor base_color_factor{1, 1, 1, 1};
    double metallic_factor{1}, roughness_factor{1};
    std::optional<TextureBinding> base_color_texture{};
    std::optional<TextureBinding> metallic_roughness_texture{};
    std::optional<TextureBinding> normal_texture{};
    double normal_scale{1};
    std::optional<TextureBinding> occlusion_texture{};
    double occlusion_strength{1};
    LinearColor emission_multiplier{};
    std::optional<TextureBinding> emissive_texture{};
    AlphaMode alpha_mode{AlphaMode::Opaque};
    float alpha_cutoff{0.5f};
    bool double_sided{};
    PathSurface path_surface{PathSurface::Diffuse};
    double ior{1.5};
    double fuzz{};
};

// Instances borrow the local mesh storage owned by this scene.
export class GltfScene
{
  public:
    Vec3 environment{}; // Constant incident radiance for path rays that leave the scene.
    std::vector<HomogeneousMedium> media{};
    GltfScene(GltfScene &&) noexcept = default;
    GltfScene &operator=(GltfScene &&) noexcept = default;
    [[nodiscard]] static GltfScene empty_scene();

    [[nodiscard]] static std::expected<GltfScene, std::string> load(const std::filesystem::path &path,
                                                                    GltfContent content);
    void check_output_path(const std::filesystem::path &path) const;
    [[nodiscard]] SurfaceMaterial surface_material(const Triangle &face, Barycentrics weights,
                                                   WeightGradients gradients = {}) const;
    [[nodiscard]] Vec3 shading_normal(const MeshInstance &instance, const Triangle &face, Barycentrics weights,
                                      WeightGradients gradients, bool back_face) const;
    [[nodiscard]] const GltfMaterial &material(const Triangle &face) const
    {
        return material(face.material_index);
    }
    [[nodiscard]] const GltfMaterial &material(std::size_t slot) const
    {
        return materials_.at(slot).value();
    }
    [[nodiscard]] SurfaceMaterial surface_material(const SphereInstance &sphere, Vec3 outward_normal) const;
    void add_spheres(std::span<const SphereSettings> spheres);
    [[nodiscard]] std::span<const SphereInstance> spheres() const & noexcept
    {
        return spheres_;
    }
    [[nodiscard]] Bounds3 bounds() const;
    // Depth and shadow passes accept opaque and covered mask samples. The filter borrows the scene.
    [[nodiscard]] SurfaceFilter solid_filter() const &;
    SurfaceFilter solid_filter() const && = delete;
    [[nodiscard]] FaceCulling face_culling(CullFace single_sided) const &;
    FaceCulling face_culling(CullFace) const && = delete;
    // Procedural meshes such as the preview floor are opaque and visible from either side.
    void add_solid_mesh(std::vector<Triangle> mesh, LinearColor color);
    void set_path_material(std::size_t gltf_index, PathSurface type, double ior, double fuzz = 0);
    void mark_sampling_target(std::uint32_t node_index);
    [[nodiscard]] const MeshInstance &instance(InstanceId id) const &
    {
        return instances_.at(node_instance_slots_.at(std::to_underlying(id)).value());
    }
    const MeshInstance &instance(InstanceId id) const && = delete;
    [[nodiscard]] std::size_t mesh_count() const noexcept
    {
        return meshes_.size();
    }
    [[nodiscard]] std::span<const MeshInstance> instances() const & noexcept
    {
        return instances_;
    }
    std::span<const MeshInstance> instances() const && = delete;

  private:
    GltfScene() = default;
    std::vector<std::filesystem::path> source_files_;
    [[nodiscard]] LinearColor sample(const TextureBinding &binding, const Triangle &face, Barycentrics weights,
                                     WeightGradients gradients) const;
    [[nodiscard]] LinearColor sample_base_color(const GltfMaterial &material, const Triangle &face,
                                                Barycentrics weights, WeightGradients gradients) const;
    std::vector<std::vector<Triangle>> meshes_;
    std::vector<SphereInstance> spheres_;
    std::vector<MeshInstance> instances_;
    std::vector<std::optional<std::size_t>> node_instance_slots_;
    std::vector<TextureImage> images_;
    std::map<std::filesystem::path, std::size_t> sphere_image_slots_;
    std::vector<std::optional<GltfMaterial>> materials_;
};
} // namespace formlight
