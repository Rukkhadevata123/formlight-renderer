module formlight.ray_query;

import std;

namespace formlight
{
namespace
{
constexpr double infinity = std::numeric_limits<double>::infinity();
using Axis = double Vec3::*;

Bounds3 triangle_bounds(const MeshInstance &instance, const Triangle &triangle)
{
    auto bounds = Bounds3::empty();
    for (const auto &vertex : triangle.vertices)
    {
        const Vec3 world = instance.transform.to_world_point(vertex.position);
        // The box is tested in world space, while the triangle is tested in instance space.
        const Vec3 padding = instance.transform.world_point_magnitudes(abs_components(vertex.position)) *
                             (8 * std::numeric_limits<double>::epsilon());
        for (const auto p : {world - padding, world + padding})
        {
            if (!(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)))
            {
                throw std::invalid_argument("BVH requires finite world-space triangle bounds");
            }
            bounds.include(p);
        }
    }
    return bounds;
}

Axis longest_axis(const Bounds3 &centers) noexcept
{
    const Vec3 extent = centers.high - centers.low;
    if (extent.x >= extent.y && extent.x >= extent.z)
    {
        return &Vec3::x;
    }
    return extent.y >= extent.z ? &Vec3::y : &Vec3::z;
}

// Closed slabs admit tangencies and flat boxes. The triangle test owns the open ray endpoints.
double entry_distance(const Bounds3 &bounds, const Ray &ray, RayInterval interval) noexcept
{
    constexpr double roundoff = 4 * std::numeric_limits<double>::epsilon();
    for (const auto axis : {&Vec3::x, &Vec3::y, &Vec3::z})
    {
        const double origin = ray.origin.*axis, direction = ray.direction.*axis;
        const double low = bounds.low.*axis, high = bounds.high.*axis;
        if (direction == 0)
        {
            if (origin < low || origin > high)
            {
                return infinity;
            }
            continue;
        }

        double near = (low - origin) / direction, far = (high - origin) / direction;
        if (direction < 0)
        {
            std::swap(near, far);
        }
        // Round the slab interval outward before clipping against another axis or a previous hit.
        interval.min = std::max(interval.min, near * (near < 0 ? 1 + roundoff : 1 - roundoff));
        interval.max = std::min(interval.max, far * (far < 0 ? 1 - roundoff : 1 + roundoff));
        if (interval.min > interval.max)
        {
            return infinity;
        }
    }
    return interval.min;
}
} // namespace

struct TriangleBvh::BuildPrimitive
{
    Bounds3 bounds;
    Vec3 center;
    Primitive source;
};

TriangleBvh::TriangleBvh(std::span<const MeshInstance> instances, SurfaceFilter accept, const FaceCulling &culling)
    : instances_(instances), accept_(std::move(accept))
{
    std::size_t count = 0;
    for (const auto &instance : instances)
    {
        count += instance.triangles.size();
    }
    if (count == 0)
    {
        return;
    }

    std::vector<BuildPrimitive> work;
    work.reserve(count);
    for (std::size_t i = 0; i < instances.size(); ++i)
    {
        const auto &instance = instances[i];
        for (std::size_t j = 0; j < instance.triangles.size(); ++j)
        {
            const auto bounds = triangle_bounds(instance, instance.triangles[j]);
            const auto cull = culling ? culling({instance.id, instance.triangles[j].id}) : CullFace::None;
            work.push_back({bounds, bounds.center(), {i, j, cull}});
        }
    }
    primitives_.reserve(count);
    build(work);
}

std::size_t TriangleBvh::build(std::span<BuildPrimitive> primitives)
{
    auto bounds = Bounds3::empty(), centers = Bounds3::empty();
    for (const auto &p : primitives)
    {
        bounds.include(p.bounds.low);
        bounds.include(p.bounds.high);
        centers.include(p.center);
    }

    const auto index = nodes_.size();
    nodes_.push_back({bounds, primitives_.size(), primitives.size()});
    constexpr std::size_t leaf_capacity = 4;
    if (primitives.size() <= leaf_capacity)
    {
        for (const auto &p : primitives)
        {
            primitives_.push_back(p.source);
        }
        return index;
    }

    const auto axis = longest_axis(centers);
    const auto key = [axis](const BuildPrimitive &p)
    { return std::tuple{p.center.*axis, p.source.instance, p.source.triangle}; };
    const auto mid = primitives.size() / 2;
    std::ranges::nth_element(primitives, primitives.begin() + static_cast<std::ptrdiff_t>(mid), {}, key);
    build(primitives.first(mid));
    const auto right = build(primitives.subspan(mid));
    nodes_[index] = {bounds, right, 0};
    return index;
}

template <TriangleBvh::Query Kind>
std::optional<RayHit> TriangleBvh::trace(const Ray &ray, RayInterval interval, RayQueryStats *stats) const
{
    if (stats)
    {
        ++stats->rays;
    }
    if (empty() || !(interval.min < interval.max) || entry_distance(nodes_.front().bounds, ray, interval) == infinity)
    {
        return std::nullopt;
    }

    std::optional<RayHit> nearest;
    const auto visit = [&](this const auto &self, std::size_t index) -> bool
    {
        const auto &node = nodes_[index];
        if (node.count > 0)
        {
            const auto leaf = std::span{primitives_}.subspan(node.first_primitive_or_right_child, node.count);
            auto previous = leaf.front().instance;
            Ray local = instances_[previous].transform.to_local_ray(ray);
            for (const auto &p : leaf)
            {
                const auto &instance = instances_[p.instance];
                if (p.instance != previous)
                {
                    local = instance.transform.to_local_ray(ray);
                    previous = p.instance;
                }
                if (stats)
                {
                    ++stats->triangle_tests;
                }
                if (const auto hit = intersect_triangle(local, instance.triangles[p.triangle], instance.id, interval,
                                                        accept_, p.cull))
                {
                    if constexpr (Kind == Query::Any)
                    {
                        nearest = hit;
                        return true;
                    }
                    keep_nearest(nearest, *hit);
                }
            }
            return false;
        }

        std::size_t first = index + 1, second = node.first_primitive_or_right_child;
        const RayInterval search{interval.min, nearest ? nearest->t : interval.max};
        double a = entry_distance(nodes_[first].bounds, ray, search);
        double b = entry_distance(nodes_[second].bounds, ray, search);
        if (b < a)
        {
            std::swap(first, second);
            std::swap(a, b);
        }
        if (a == infinity)
        {
            return false;
        }
        if (self(first))
        {
            return true;
        }
        // The other subtree may contain a closer hit, or an equal t with a smaller surface ID.
        return b != infinity && (!nearest || b <= nearest->t) && self(second);
    };
    visit(0);
    return nearest;
}

std::optional<RayHit> TriangleBvh::intersect(const Ray &ray, RayInterval interval, RayQueryStats *stats) const
{
    return trace<Query::Closest>(ray, interval, stats);
}

bool TriangleBvh::occluded(const Ray &ray, RayInterval interval, RayQueryStats *stats) const
{
    return trace<Query::Any>(ray, interval, stats).has_value();
}
} // namespace formlight
