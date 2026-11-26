export module formlight.gltf;

import std;
export import formlight.mesh_instance;
export import formlight.texture;

namespace formlight
{
export enum class GltfContent
{
    Geometry,
    BaseColor
};

struct BaseColorTexture
{
    std::size_t image;
    TextureSampler sampler;
};

export enum class AlphaMode
{
    Opaque,
    Mask,
    Blend
};

export struct BaseColorMaterial
{
    LinearColor factor{1, 1, 1, 1};
    std::optional<BaseColorTexture> texture;
    AlphaMode alpha_mode{AlphaMode::Opaque};
    float alpha_cutoff{0.5f};
    bool double_sided{};
};

// Instances borrow the local mesh storage owned by this scene.
export class GltfScene
{
  public:
    GltfScene(GltfScene &&) noexcept = default;
    GltfScene &operator=(GltfScene &&) noexcept = default;

    [[nodiscard]] static std::expected<GltfScene, std::string> load_glb(const std::filesystem::path &path,
                                                                        GltfContent content);
    [[nodiscard]] LinearColor base_color(const Triangle &face, Barycentrics weights) const;
    [[nodiscard]] const BaseColorMaterial &material(const Triangle &face) const
    {
        return materials_.at(face.material_index).value();
    }
    // Depth and shadow passes accept opaque and covered mask samples. The filter borrows the scene.
    [[nodiscard]] SurfaceFilter solid_filter() const &;
    SurfaceFilter solid_filter() const && = delete;
    void add_solid_mesh(std::vector<Triangle> mesh, LinearColor color);
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
    [[nodiscard]] LinearColor sample_base_color(const BaseColorMaterial &material, const Triangle &face,
                                                Barycentrics weights) const;
    std::vector<std::vector<Triangle>> meshes_;
    std::vector<MeshInstance> instances_;
    std::vector<std::optional<std::size_t>> node_instance_slots_;
    std::vector<std::optional<ColorImage>> images_;
    std::vector<std::optional<BaseColorMaterial>> materials_;
};
} // namespace formlight
