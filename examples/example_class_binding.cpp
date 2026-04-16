/**
 * @file example_class_binding.cpp
 * @brief Demonstrates ClassBinder features.
 *
 * Covers:
 * - Constructor (default + overloaded + factory)
 * - Destructor (custom via C API destroy)
 * - Method binding (member function, lambda, free function as method)
 * - Property (read-write, read-only)
 * - Static method
 * - Enum values
 * - Inheritance (ColorPoint extends Point)
 */

#include "common.hpp"

int main(int argc, char* argv[]) {
    JsRuntime rt;
    JsContext ctx(rt);
    registerPrint(ctx);

    // --- Bind Point class ---
    {
        ClassBinder<Point> binder(ctx, "Point");
        binder
            .constructor<void(), void(double, double)>()
            .method("getX", &Point::getX)
            .method("getY", &Point::getY)
            .method("setX", &Point::setX)
            .method("setY", &Point::setY)
            .method("length", &Point::length)
            .method("toString", &Point::toString)
            .method("add",
                [](Point& self, double ox, double oy) -> std::string {
                    Point result(self.getX() + ox, self.getY() + oy);
                    return result.toString();
                })
            .method("distFromOrigin", &point_distance_from_origin)
            .property("x",
                [](const Point& p) -> double { return p.getX(); },
                [](Point& p, double v) { p.setX(v); })
            .property("y",
                [](const Point& p) -> double { return p.getY(); },
                [](Point& p, double v) { p.setY(v); })
            .property_readonly("len", &Point::length)
            .static_method("origin", [](JSContext* ctx) -> JSValue {
                Point* p = new Point(0, 0);
                JSClassID cid = ClassRegistry::classId<Point>();
                JSValue proto = JS_GetClassProto(ctx, cid);
                JSValue obj = JS_NewObjectProtoClass(ctx, proto, cid);
                JS_FreeValue(ctx, proto);
                auto* pd = ClassRegistry::makeOwned<Point>(p);
                JS_SetOpaque(obj, pd);
                return obj;
            })
            .install();
    }

    // --- Bind ColorPoint (inherits Point) ---
    {
        ClassBinder<ColorPoint, Point> binder(ctx, "ColorPoint");
        binder
            .constructor<void(double, double, Color)>()
            .method("getColor", &ColorPoint::getColor)
            .method("setColor", &ColorPoint::setColor)
            .enum_value("Red", Color::Red)
            .enum_value("Green", Color::Green)
            .enum_value("Blue", Color::Blue)
            .install();
    }

    // --- Bind CHandle via factory constructor ---
    {
        ClassBinder<CHandle> binder(ctx, "CHandle");
        binder
            .constructor2(&c_api_create)
            .destructor(&c_api_destroy)
            .method("getData", &c_api_get_data)
            .method("setData", &c_api_set_data)
            .property("data",
                [](CHandle* self) -> std::string { return self->data; },
                [](CHandle* self, const std::string& d) { self->data = d; })
            .property_readonly("id",
                [](CHandle* self) -> int { return self->id; })
            .install();
    }

    // --- Run the JS test script ---
    std::string scriptDir = argc > 1 ? argv[1] : "examples/scripts";
    return runScriptFile(ctx, scriptDir + "/test_class_binding.js");
}
