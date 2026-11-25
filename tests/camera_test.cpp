import std;
import formlight.camera;

namespace
{
using namespace formlight;

constexpr CameraView front_view{
    .position = {0, 0, 0},
    .target = {0, 0, -1},
    .up = {0, 1, 0},
    .vertical_fov_radians = std::numbers::pi / 2,
};

void expect_near(double actual, double expected, std::string_view label)
{
    if (!(std::abs(actual - expected) <= 1e-12))
    {
        throw std::runtime_error(std::format("{}: expected {}, got {}", label, expected, actual));
    }
}

void check_projection()
{
    const PinholeCamera camera{front_view, {8, 4}};
    // With a 90-degree vertical field of view, the top and bottom edges are at Y = +/-2 at depth 2.
    const auto top_right = camera.project({4, 2, -2}).value();
    expect_near(top_right.image.x, 8, "right image edge");
    expect_near(top_right.image.y, 0, "top image edge");
    expect_near(top_right.view_depth, 2, "forward depth");

    const auto off_screen = camera.project({-5, -3, -2}).value();
    expect_near(off_screen.image.x, -1, "left of viewport");
    expect_near(off_screen.image.y, 5, "below viewport");
    if (camera.project({1, 0, 0}) || camera.project({0, 0, 1}))
    {
        throw std::runtime_error("A point on or behind the eye plane was projected");
    }
    if (camera.project({1, 0, -std::numeric_limits<double>::denorm_min()}))
    {
        throw std::runtime_error("Projection returned unrepresentable image coordinates");
    }
}

void check_primary_rays()
{
    constexpr CameraView view{
        .position = {3, 2, 1},
        .target = {7, 2, 1},
        .up = {1, 2, 0},
        .vertical_fov_radians = std::numbers::pi / 3,
    };
    const PinholeCamera camera{view, {9, 5}};
    const auto center = pixel_center(4, 2);
    expect_near(center.x, 4.5, "center column");
    expect_near(center.y, 2.5, "center row");

    // This camera looks along +X, with +Y up and +Z to its right.
    const auto center_ray = camera.ray_for_sample(center);
    expect_near(center_ray.direction.x, 1, "center ray X");
    expect_near(center_ray.direction.y, 0, "center ray Y");
    expect_near(center_ray.direction.z, 0, "center ray Z");
    const auto edge_ray = camera.ray_for_sample({4.5, 0});
    expect_near(edge_ray.direction.x, std::sqrt(3.0) / 2, "top ray X");
    expect_near(edge_ray.direction.y, 0.5, "top ray Y");
    const auto right = camera.project({7, 2, 5}).value();
    expect_near(right.image.x, center.x + 2.5 * std::sqrt(3.0), "rotated camera right");

    for (const auto sample : {center, pixel_center(0, 0), pixel_center(8, 4), ImagePoint{2.25, 1.75}})
    {
        const auto ray = camera.ray_for_sample(sample);
        expect_near(ray.origin.x, view.position.x, "origin X");
        expect_near(ray.origin.y, view.position.y, "origin Y");
        expect_near(ray.origin.z, view.position.z, "origin Z");
        expect_near(dot(ray.direction, ray.direction), 1, "unit ray direction");
        for (const double distance : {1.0, 7.0})
        {
            const auto projected = camera.project(ray.at(distance)).value();
            expect_near(projected.image.x, sample.x, "round-trip image X");
            expect_near(projected.image.y, sample.y, "round-trip image Y");
            expect_near(projected.view_depth, distance * ray.direction.x, "off-axis view depth");
        }
    }
}

void expect_invalid(const CameraView &view, ImageExtent extent, std::string_view label)
{
    try
    {
        PinholeCamera{view, extent};
    }
    catch (const std::invalid_argument &)
    {
        return;
    }
    throw std::runtime_error(std::format("Invalid camera accepted: {}", label));
}

void check_invalid_views()
{
    constexpr ImageExtent extent{8, 4};
    expect_invalid(front_view, {0, 4}, "zero width");
    expect_invalid(front_view, {8, -1}, "negative height");
    auto view = front_view;
    view.target = view.position;
    expect_invalid(view, extent, "coincident position and target");

    view = front_view;
    view.up = {0, 0, 0};
    expect_invalid(view, extent, "zero up");
    view.up = {0, 0, 2};
    expect_invalid(view, extent, "collinear up");

    view = front_view;
    for (const double fov : {0.0, std::numbers::pi, std::numeric_limits<double>::quiet_NaN()})
    {
        view.vertical_fov_radians = fov;
        expect_invalid(view, extent, "field of view");
    }
}
} // namespace

auto main() -> int
{
    try
    {
        check_projection();
        check_primary_rays();
        check_invalid_views();
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
