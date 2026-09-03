#pragma once

#include <cmath>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

namespace gfx::sky {
    struct SphereHit {
        bool hit = false;
        double distance = 0.0;
        glm::dvec3 position{0.0};
    };

    inline SphereHit intersect_sphere(const glm::dvec3& origin, const glm::dvec3& direction,
                                      const glm::dvec3& center, double radius) {
        if (radius <= 0.0 || glm::dot(direction, direction) <= 1e-24) return {};
        const glm::dvec3 d = glm::normalize(direction);
        const glm::dvec3 offset = origin - center;
        const double b = glm::dot(offset, d);
        const double c = glm::dot(offset, offset) - radius * radius;
        const double discriminant = b * b - c;
        if (discriminant < 0.0) return {};

        const double root = std::sqrt(discriminant);
        const double t0 = -b - root;
        const double t1 = -b + root;
        constexpr double epsilon = 1e-9;
        const double t = t0 > epsilon ? t0 : (t1 > epsilon ? t1 : -1.0);
        if (t <= 0.0) return {};
        return {.hit = true, .distance = t, .position = origin + d * t};
    }
}
