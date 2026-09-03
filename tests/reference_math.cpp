#include <array>
#include <cmath>
#include <iostream>

namespace {
    struct Vec3 {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    Vec3 operator+(const Vec3 a, const Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    Vec3 operator-(const Vec3 a, const Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    Vec3 operator*(const Vec3 value, const double scalar) { return {value.x * scalar, value.y * scalar, value.z * scalar}; }

    double dot(const Vec3 a, const Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    double length(const Vec3 value) { return std::sqrt(dot(value, value)); }
    Vec3 normalize(const Vec3 value) { return value * (1.0 / length(value)); }

    bool intersectSphere(
        const Vec3 origin,
        const Vec3 directionInput,
        const Vec3 center,
        const double radius,
        Vec3& hit
    ) {
        const Vec3 direction = normalize(directionInput);
        const Vec3 offset = origin - center;
        const double b = dot(offset, direction);
        const double c = dot(offset, offset) - radius * radius;
        const double discriminant = b * b - c;
        if (discriminant < 0.0) return false;

        const double root = std::sqrt(discriminant);
        const double nearT = -b - root;
        const double farT = -b + root;
        const double t = nearT > 1e-8 ? nearT : farT;
        if (t <= 1e-8) return false;
        hit = origin + direction * t;
        return true;
    }

    bool close(const Vec3 a, const Vec3 b, const double epsilon = 1e-10) {
        return length(a - b) <= epsilon;
    }

    bool run() {
        Vec3 hit;
        if (!intersectSphere({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 3.0, hit)) return false;
        if (!close(hit, {3.0, 0.0, 0.0})) return false;

        if (!intersectSphere({-5.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 2.0, hit)) return false;
        if (!close(hit, {-2.0, 0.0, 0.0})) return false;

        if (intersectSphere({-5.0, 5.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 2.0, hit)) return false;

        if (!intersectSphere({0.2, -0.3, 0.4}, {0.3, 0.7, -0.2}, {1.0, 2.0, -1.0}, 10.0, hit)) return false;
        const double radiusError = std::abs(length(hit - Vec3{1.0, 2.0, -1.0}) - 10.0);
        if (radiusError > 1e-10) return false;
        return true;
    }
}

int main() {
    if (!run()) {
        std::cerr << "ray-sphere reference tests failed\n";
        return 1;
    }
    std::cout << "ray-sphere reference tests passed\n";
    return 0;
}
