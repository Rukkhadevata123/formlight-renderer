export module formlight.gltf;

import std;
export import formlight.mesh_instance;

namespace formlight
{
// Owns the local meshes referenced by the instances of the selected glTF scene.
export class GltfGeometry
{
  public:
    GltfGeometry(GltfGeometry &&) noexcept = default;
    GltfGeometry &operator=(GltfGeometry &&) noexcept = default;

    [[nodiscard]] static std::expected<GltfGeometry, std::string> load_glb(const std::filesystem::path &path);
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
    GltfGeometry() = default;
    std::vector<std::vector<Triangle>> meshes_;
    std::vector<MeshInstance> instances_;
};
} // namespace formlight
