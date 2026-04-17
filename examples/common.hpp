/**
 * @file common.hpp
 * @brief Shared definitions for all QuickJSBinder examples.
 *
 * Includes:
 * - Common headers and using declarations
 * - Example native classes (Point, ColorPoint, CHandle)
 * - Utility helpers (registerPrint, runScriptFile, evalAndCheck)
 */
#pragma once

#include <cstdint>
#include <qjsbind/qjsbind.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace qjsbind;

// ============================================================================
// Example native classes
// ============================================================================

/// A simple 2D point class.
class Point {
public:
    Point() : x_(0), y_(0) { std::cout << "Point() default\n"; }
    Point(double x, double y) : x_(x), y_(y) {
        std::cout << "Point(" << x << ", " << y << ")\n";
    }
    ~Point() { std::cout << "~Point(" << x_ << ", " << y_ << ")\n"; }

    double getX() const { return x_; }
    void setX(double x) { x_ = x; }
    double getY() const { return y_; }
    void setY(double y) { y_ = y; }

    double length() const { return std::sqrt(x_ * x_ + y_ * y_); }

    std::string toString() const {
        return "Point(" + std::to_string(x_) + ", " + std::to_string(y_) + ")";
    }

    Point add(const Point& other) const {
        return Point(x_ + other.x_, y_ + other.y_);
    }
    Point addScalar(double s) const {
        return Point(x_ + s, y_ + s);
    }

    static Point origin() { return Point(0, 0); }

private:
    double x_, y_;
};
QJSBIND_DECLARE_CONVERTER(Point);

/// Color enum for testing.
enum class Color { Red = 0, Green = 1, Blue = 2 };

/// Derived class to test inheritance.
class ColorPoint : public Point {
public:
    ColorPoint(double x, double y, Color c) : Point(x, y), color_(c) {
        std::cout << "ColorPoint(" << x << ", " << y << ", " << static_cast<int>(c) << ")\n";
    }
    ~ColorPoint() { std::cout << "~ColorPoint\n"; }

    Color getColor() const { return color_; }
    void setColor(Color c) { color_ = c; }
    void setColor(std::string c) { color_ = Color::Red; /* Dummy */ }

private:
    Color color_;
};
QJSBIND_DECLARE_CONVERTER(ColorPoint);

// ============================================================================
// Simulated C API (for factory constructor demo)
// ============================================================================

struct CHandle {
    int id;
    std::string data;
};

inline CHandle* c_api_create(int id) {
    auto* h = new CHandle{id, "handle-" + std::to_string(id)};
    std::cout << "c_api_create(" << id << ") -> " << h << "\n";
    return h;
}

inline void c_api_destroy(CHandle* h) {
    std::cout << "c_api_destroy(" << h->id << ")\n";
    delete h;
}

inline std::string c_api_get_data(CHandle* self) {
    return self->data;
}

inline void c_api_set_data(CHandle* self, const std::string& data) {
    self->data = data;
}

/// A free function taking Point& as this.
inline double point_distance_from_origin(const Point& p) {
    return p.length();
}

// ============================================================================
// Utility helpers
// ============================================================================

/**
 * @brief Register a global `print` function via JsProxy.
 */
inline void registerPrint(JsContext& ctx) {
    ctx["print"] = [](JSContext*, std::string msg) {
        std::cout << "[JS] " << msg << "\n";
    };
}

/**
 * @brief Read a file's contents into a string.
 * @param path File path.
 * @return File contents, or empty string on failure.
 */
inline std::string readFileContents(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        std::cerr << "Failed to open file: " << path << "\n";
        return {};
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

/**
 * @brief Evaluate a JS script file and handle exceptions.
 * @param ctx The JS context.
 * @param path Path to the .js file.
 * @return 0 on success, 1 on exception.
 */
inline int runScriptFile(JsContext& ctx, const std::string& path) {
    std::string code = readFileContents(path);
    if (code.empty()) {
        std::cerr << "Empty or unreadable script: " << path << "\n";
        return 1;
    }
    JsValue result = ctx.eval(code.c_str(), path.c_str());
    if (result.isException()) {
        JsValue exc = JsValue::adopt(ctx, JS_GetException(ctx));
        std::cerr << "JS Exception in " << path << ": " << exc.toString() << std::endl;
        JsValue stack = exc.getProperty("stack");
        if (!stack.isUndefined()) {
            std::cerr << stack.toString() << std::endl;
        }
        return 1;
    }
    return 0;
}

/**
 * @brief Evaluate an inline JS script and handle exceptions.
 * @param ctx The JS context.
 * @param code The JS code string.
 * @param filename Logical filename for error messages.
 * @return 0 on success, 1 on exception.
 */
inline int evalAndCheck(JsContext& ctx, const char* code, const char* filename = "<eval>") {
    JsValue result = ctx.eval(code, filename);
    if (result.isException()) {
        JsValue exc = JsValue::adopt(ctx, JS_GetException(ctx));
        std::cerr << "JS Exception: " << exc.toString() << std::endl;
        JsValue stack = exc.getProperty("stack");
        if (!stack.isUndefined()) {
            std::cerr << stack.toString() << std::endl;
        }
        return 1;
    }
    return 0;
}
