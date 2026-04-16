/**
 * @file native_plugin.cpp
 * @brief A native ES module plugin (.so/.dylib/.dll) built with QuickJSBinder.
 *
 * This shared library is loaded by QuickJS via `import ... from "./native_plugin.so"`
 * (or .dylib on macOS). It exports:
 *
 * Functions:
 *   - add(a, b)      — returns a + b
 *   - mul(a, b)      — returns a * b
 *   - greet(name)    — returns "Hello from native, <name>!"
 *
 * Constants:
 *   - VERSION        — version string
 *   - GOLDEN_RATIO   — φ ≈ 1.618
 *
 * Class:
 *   - Vec3           — 3D vector with x, y, z, length(), dot(), toString()
 *
 * @note Compile as a shared library (set_kind("shared") in xmake).
 */

#include <qjsbind/qjsbind.hpp>

#include <cmath>
#include <string>

using namespace qjsbind;

// ============================================================================
// Native class to export: Vec3
// ============================================================================

class Vec3 {
public:
    Vec3() : x_(0), y_(0), z_(0) {}
    Vec3(double x, double y, double z) : x_(x), y_(y), z_(z) {}

    double getX() const { return x_; }
    double getY() const { return y_; }
    double getZ() const { return z_; }

    void setX(double x) { x_ = x; }
    void setY(double y) { y_ = y; }
    void setZ(double z) { z_ = z; }

    double length() const { return std::sqrt(x_ * x_ + y_ * y_ + z_ * z_); }

    double dot(const Vec3& other) const {
        return x_ * other.x_ + y_ * other.y_ + z_ * other.z_;
    }

    Vec3 cross(const Vec3& other) const {
        return Vec3(
            y_ * other.z_ - z_ * other.y_,
            z_ * other.x_ - x_ * other.z_,
            x_ * other.y_ - y_ * other.x_
        );
    }

    Vec3 normalize() const {
        double len = length();
        if (len == 0) return Vec3(0, 0, 0);
        return Vec3(x_ / len, y_ / len, z_ / len);
    }

    std::string toString() const {
        return "Vec3(" + std::to_string(x_) + ", "
                       + std::to_string(y_) + ", "
                       + std::to_string(z_) + ")";
    }

private:
    double x_, y_, z_;
};

QJSBIND_DECLARE_CONVERTER(Vec3);

// ============================================================================
// Module setup — one function does everything
// ============================================================================

static void setupModule(NativeModuleExport& mod) {
    // -- Export functions --
    mod.function("add", [](double a, double b) { return a + b; });
    mod.function("mul", [](double a, double b) { return a * b; });
    mod.function("greet", [](std::string name) -> std::string {
        return "Hello from native, " + name + "!";
    });

    // -- Export constants --
    mod.value("VERSION", std::string("1.0.0"));
    mod.value("GOLDEN_RATIO", 1.6180339887498949);

    // -- Export a class (Vec3) --
    mod.class_<Vec3>("Vec3", [](ClassBinder<Vec3>& cls) {
        cls.constructor<void(), void(double, double, double)>()
           .method("length", &Vec3::length)
           .method("dot", &Vec3::dot)
           .method("cross", &Vec3::cross)
           .method("normalize", &Vec3::normalize)
           .method("toString", &Vec3::toString)
           .property("x",
               [](const Vec3& v) -> double { return v.getX(); },
               [](Vec3& v, double val) { v.setX(val); })
           .property("y",
               [](const Vec3& v) -> double { return v.getY(); },
               [](Vec3& v, double val) { v.setY(val); })
           .property("z",
               [](const Vec3& v) -> double { return v.getZ(); },
               [](Vec3& v, double val) { v.setZ(val); });
    });
}

// Auto-generate js_init_module — no need to manually list export names!
QJSBIND_NATIVE_MODULE(setupModule)
