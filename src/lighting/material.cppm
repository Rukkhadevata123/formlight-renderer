export module formlight.material;

import std;
import formlight.geometry;
import formlight.image;

export namespace formlight
{
struct SurfaceMaterial
{
    LinearColor base_color;
    LinearColor emission;
    double metallic{1}, roughness{1};
};

// Terms shared by every light sample at the same surface point and camera direction.
class MetallicRoughnessBrdf
{
  public:
    MetallicRoughnessBrdf(const SurfaceMaterial &material, Vec3 unit_normal, Vec3 toward_camera) noexcept;
    [[nodiscard]] Vec3 evaluate_times_cosine(Vec3 toward_light) const noexcept;

  private:
    Vec3 normal_, view_, f0_{}, diffuse_{};
    double n_dot_v_, alpha_squared_, smith_k_, view_denominator_;
};

MetallicRoughnessBrdf::MetallicRoughnessBrdf(const SurfaceMaterial &material, Vec3 unit_normal,
                                             Vec3 toward_camera) noexcept
    : normal_(unit_normal), view_(toward_camera), n_dot_v_(std::clamp(dot(normal_, view_), 0.0, 1.0))
{
    const auto &base = material.base_color;
    const Vec3 albedo{base.r, base.g, base.b};
    constexpr Vec3 dielectric{0.04, 0.04, 0.04};
    f0_ = dielectric + (albedo - dielectric) * material.metallic;
    diffuse_ = albedo * ((1.0 - material.metallic) / std::numbers::pi);

    // A finite minimum lobe width represents a smooth surface under point-sampled lighting.
    const double roughness = std::max(material.roughness, 0.045);
    const double alpha = roughness * roughness;
    alpha_squared_ = alpha * alpha;
    smith_k_ = (roughness + 1) * (roughness + 1) / 8;
    view_denominator_ = n_dot_v_ * (1 - smith_k_) + smith_k_;
}

Vec3 MetallicRoughnessBrdf::evaluate_times_cosine(Vec3 toward_light) const noexcept
{
    const double n_dot_l = std::clamp(dot(normal_, toward_light), 0.0, 1.0);
    if (n_dot_l == 0 || n_dot_v_ == 0)
    {
        return {};
    }
    const Vec3 sum = view_ + toward_light;
    const double half_length = std::sqrt(dot(sum, sum));
    if (half_length == 0)
    {
        return {};
    }
    const Vec3 half = sum / half_length;
    const double n_dot_h = std::clamp(dot(normal_, half), 0.0, 1.0);
    const double nh2 = n_dot_h * n_dot_h;
    const double denom = (1 - nh2) + nh2 * alpha_squared_;
    const double distribution = alpha_squared_ / (std::numbers::pi * denom * denom);
    // Schlick-Smith G / (4 N.V N.L) after cancelling the two cosine factors.
    const double light_denominator = n_dot_l * (1 - smith_k_) + smith_k_;
    const double specular = distribution / (4 * view_denominator_ * light_denominator);
    const double grazing = 1 - std::clamp(dot(view_, half), 0.0, 1.0);
    const double g2 = grazing * grazing;
    const Vec3 fresnel = f0_ + (Vec3{1, 1, 1} - f0_) * (g2 * g2 * grazing);
    const Vec3 diffuse{(1 - fresnel.x) * diffuse_.x, (1 - fresnel.y) * diffuse_.y, (1 - fresnel.z) * diffuse_.z};
    return (diffuse + fresnel * specular) * n_dot_l;
}
} // namespace formlight
