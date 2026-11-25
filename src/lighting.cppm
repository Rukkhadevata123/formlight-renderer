export module formlight.lighting;

import std;
import formlight.geometry;
import formlight.image;

export namespace formlight
{
// White directional light, scaled so a surface facing the light returns its base color.
class DirectionalLight
{
  public:
    explicit DirectionalLight(Vec3 toward_light)
        : toward_light_(unit_vector(toward_light, "Light direction must be finite and nonzero"))
    {
    }

    [[nodiscard]] Vec3 toward_light() const noexcept
    {
        return toward_light_;
    }

    [[nodiscard]] LinearColor diffuse(LinearColor base_color, Vec3 unit_world_normal) const noexcept
    {
        const auto cosine = static_cast<float>(std::max(0.0, dot(unit_world_normal, toward_light_)));
        return {base_color.r * cosine, base_color.g * cosine, base_color.b * cosine, base_color.a};
    }

  private:
    Vec3 toward_light_;
};
} // namespace formlight
