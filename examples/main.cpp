/**
 * @file main.cpp
 * @brief Comprehensive example running all QuickJSBinder feature tests.
 *
 * This is the all-in-one example. Individual feature examples are in
 * separate files (example_class_binding.cpp, example_proxy.cpp,
 * example_module.cpp).
 *
 * The JS test scripts are loaded from examples/scripts/
 */

#include "common.hpp"

int main(int argc, char* argv[]) {
    JsRuntime rt;
    JsContext ctx(rt);
    registerPrint(ctx);

    // --- Register global functions via JsProxy ---
    ctx["add"] = [](double a, double b) { return a + b; };
    ctx["mul"] = [](double a, double b) { return a * b; };
    ctx["greeting"] = std::string("Hello from C++");
    ctx["magicNumber"] = 42;

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

    // --- Module: math2 ---
    {
        JsModule mathMod(ctx, "math2");
        mathMod
            .function("add", [](double a, double b) { return a + b; })
            .function("sub", [](double a, double b) { return a - b; })
            .function("mul", [](double a, double b) { return a * b; })
            .value("PI", 3.14159265358979)
            .value("E", 2.71828182845905);

        auto& trig = mathMod.submodule("trig");
        trig.function("sin", [](double x) { return std::sin(x); })
            .function("cos", [](double x) { return std::cos(x); });

        mathMod.install();
    }

    // --- Proxy: nested config ---
    ctx["config"] = JsValue::adopt(ctx, JS_NewObject(ctx));
    ctx["config"]["debug"] = true;
    ctx["config"]["version"] = std::string("1.0.0");
    ctx["config"]["maxRetries"] = 3;

    // --- C++ calling JS via proxy ---
    {
        double result = ctx["math2"]["add"](10.0, 20.0).get<double>();
        std::cout << "[C++] math2.add(10, 20) = " << result << "\n";

        double sinVal = ctx["math2"]["trig"]["sin"](1.5708).get<double>();
        std::cout << "[C++] math2.trig.sin(PI/2) = " << sinVal << "\n";

        JsValue piVal = ctx["math2"]["PI"].get();
        std::cout << "[C++] math2.PI = " << piVal.toDouble() << "\n";
    }

    // --- JsValue::operator[] and JsContext::getModule / module ---
    {
        // getModule: retrieve installed module as JsValue
        JsValue mathVal = ctx.getModule("math2");
        std::cout << "[C++] getModule(\"math2\") isObject: " << std::boolalpha
                  << mathVal.isObject() << "\n";

        // JsValue::operator[] for proxy access
        double e = mathVal["E"].get<double>();
        std::cout << "[C++] mathVal[\"E\"] = " << e << "\n";

        // module(): retrieve as proxy directly
        double pi = ctx.module("math2")["PI"].get<double>();
        std::cout << "[C++] ctx.module(\"math2\")[\"PI\"] = " << pi << "\n";
    }

    // --- Run all JS test scripts ---
    std::string scriptDir = argc > 1 ? argv[1] : "examples/scripts";
    int rc = 0;
    rc |= runScriptFile(ctx, scriptDir + "/test_class_binding.js");
    rc |= runScriptFile(ctx, scriptDir + "/test_proxy.js");
    rc |= runScriptFile(ctx, scriptDir + "/test_module.js");
    return rc;
}
