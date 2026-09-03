#include <gfx/sky/domain.hpp>

#include <cmath>
#include <cstdio>
#include <random>

namespace {
    bool near(double a, double b, double epsilon = 1e-9) { return std::abs(a - b) <= epsilon; }

    int fail(const char* message) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
}

int main() {
    using gfx::sky::intersect_sphere;

    {
        const auto hit = intersect_sphere({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 10.0);
        if (!hit.hit || !near(hit.distance, 10.0)) return fail("inside-origin ray should hit exit at radius");
    }
    {
        const auto hit = intersect_sphere({20.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 10.0);
        if (!hit.hit || !near(hit.distance, 10.0)) return fail("outside ray must select nearest positive root");
    }
    {
        const auto hit = intersect_sphere({20.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 10.0);
        if (hit.hit) return fail("outward ray should miss");
    }
    {
        if (intersect_sphere({0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 10.0).hit)
            return fail("zero direction must not produce a hit");
        if (intersect_sphere({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 0.0).hit)
            return fail("non-positive radius must not produce a hit");
    }
    {
        const auto hit = intersect_sphere({10.0, 10.0, 0.0}, {0.0, -1.0, 0.0}, {0.0, 0.0, 0.0}, 10.0);
        if (!hit.hit || !near(glm::length(hit.position), 10.0, 1e-8)) return fail("tangent/boundary case residual");
    }

    std::mt19937_64 rng(0x534b595245534541ull);
    std::uniform_real_distribution<double> coordinate(-25.0, 25.0);
    std::uniform_real_distribution<double> radius_distribution(1.0, 30.0);
    for (int i = 0; i < 20000; ++i) {
        const glm::dvec3 center(coordinate(rng), coordinate(rng), coordinate(rng));
        const double radius = radius_distribution(rng);
        glm::dvec3 direction(coordinate(rng), coordinate(rng), coordinate(rng));
        if (glm::dot(direction, direction) < 1e-12) direction = {1.0, 0.0, 0.0};
        const glm::dvec3 origin(coordinate(rng), coordinate(rng), coordinate(rng));
        const auto hit = intersect_sphere(origin, direction, center, radius);
        if (!hit.hit) continue;
        const double residual = std::abs(glm::length(hit.position - center) - radius);
        if (residual > 1e-8 * std::max(1.0, radius)) return fail("randomized sphere residual too large");
        if (hit.distance <= 0.0) return fail("randomized hit returned non-positive distance");
    }

    std::puts("gfx-research-sky domain tests passed");
    return 0;
}
