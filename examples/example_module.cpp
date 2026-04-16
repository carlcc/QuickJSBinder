/**
 * @file example_module.cpp
 * @brief Demonstrates JsModule features.
 *
 * Covers:
 * - Module creation with function/value/variable registration
 * - Nested sub-modules
 * - ClassBinder::installInto (install class into a module)
 * - JsModule::value() to retrieve the module's JSValue for dynamic scenarios
 * - C++ calling module functions via proxy
 */

#include "common.hpp"

int main(int argc, char* argv[]) {
    JsRuntime rt;
    JsContext ctx(rt);
    registerPrint(ctx);

    // --- Create a math module ---
    {
        JsModule mathMod(ctx, "math2");
        mathMod
            .function("add", [](double a, double b) { return a + b; })
            .function("sub", [](double a, double b) { return a - b; })
            .function("mul", [](double a, double b) { return a * b; })
            .value("PI", 3.14159265358979)
            .value("E", 2.71828182845905);

        // Nested sub-module
        auto& trig = mathMod.submodule("trig");
        trig.function("sin", [](double x) { return std::sin(x); })
            .function("cos", [](double x) { return std::cos(x); });

        mathMod.install();
    }

    // --- Install a class into a module ---
    {
        JsModule geom(ctx, "geom");

        ClassBinder<Point> binder(ctx, "Point");
        binder
            .constructor<void(), void(double, double)>()
            .method("getX", &Point::getX)
            .method("getY", &Point::getY)
            .method("length", &Point::length)
            .method("toString", &Point::toString)
            .property("x",
                [](const Point& p) -> double { return p.getX(); },
                [](Point& p, double v) { p.setX(v); })
            .property("y",
                [](const Point& p) -> double { return p.getY(); },
                [](Point& p, double v) { p.setY(v); })
            .installInto(geom.object());

        geom.install();
    }

    // --- Demonstrate JsModule::jsValue() for dynamic binding ---
    // This is important for game engines where scripts are loaded dynamically
    // and modules need to be passed around or stored as JSValue.
    {
        JsModule engine(ctx, "Engine");
        engine
            .function("log", [](std::string msg) {
                std::cout << "[Engine] " << msg << "\n";
            })
            .variable("frameCount", 0)
            .value("version", std::string("2.0.0"));

        // Get the module's JsValue BEFORE install — useful for passing
        // the module object to other C++ systems or storing it.
        JsValue engineVal = engine.jsValue();
        std::cout << "[C++] Engine module isObject: " << std::boolalpha
                  << engineVal.isObject() << "\n";

        // You can also use the JSValue to set properties dynamically
        JS_SetPropertyStr(ctx, engineVal.value(), "dynamicProp",
                          JS_NewString(ctx, "set-before-install"));

        engine.install();

        // After install, the module is in global. We can still use the
        // JsValue reference:
        JsValue engineVal2 = engine.jsValue();
        JsValue verProp = engineVal2.getProperty("version");
        std::cout << "[C++] Engine.version = " << verProp.toString() << "\n";

        JsValue dynProp = engineVal2.getProperty("dynamicProp");
        std::cout << "[C++] Engine.dynamicProp = " << dynProp.toString() << "\n";
    }

    // --- Demonstrate JsModule::operator[] (proxy on module) ---
    // JsModule now supports sol2-like syntax for direct property access.
    {
        JsModule graphics(ctx, "Graphics");
        graphics
            .function("draw", [](std::string shape) {
                std::cout << "[Graphics] Drawing: " << shape << "\n";
            })
            .value("maxFPS", 120)
            .variable("currentFPS", 60);

        graphics.install();

        // Use operator[] directly on the module:
        (void)graphics["draw"](std::string("circle"));
        int fps = graphics["maxFPS"].get<int>();
        std::cout << "[C++] Graphics.maxFPS (via module proxy) = " << fps << "\n";

        // Set a new property via module proxy:
        graphics["renderMode"] = std::string("deferred");
    }

    // --- Demonstrate JsContext::getModule() and JsContext::module() ---
    // Retrieve a module after installation for dynamic manipulation.
    {
        // getModule() returns a JsValue
        JsValue mathMod = ctx.getModule("math2");
        std::cout << "[C++] getModule(\"math2\") isObject: " << std::boolalpha
                  << mathMod.isObject() << "\n";

        // JsValue::operator[] — proxy on JsValue
        double pi = mathMod["PI"].get<double>();
        std::cout << "[C++] mathMod[\"PI\"] (via JsValue proxy) = " << pi << "\n";
        double addResult = mathMod["add"](100.0, 200.0).get<double>();
        std::cout << "[C++] mathMod[\"add\"](100, 200) = " << addResult << "\n";

        // module() returns a JsProxy directly — even more concise
        auto engineProxy = ctx.module("Engine");
        (void)engineProxy["log"](std::string("called via ctx.module() proxy"));
        std::string ver = engineProxy["version"].get<std::string>();
        std::cout << "[C++] ctx.module(\"Engine\")[\"version\"] = " << ver << "\n";

        // Can also use module() for one-liners:
        int gfxFps = ctx.module("Graphics")["maxFPS"].get<int>();
        std::cout << "[C++] ctx.module(\"Graphics\")[\"maxFPS\"] = " << gfxFps << "\n";

        // Verify dynamically set property is visible
        std::string mode = ctx.module("Graphics")["renderMode"].get<std::string>();
        std::cout << "[C++] ctx.module(\"Graphics\")[\"renderMode\"] = " << mode << "\n";
    }

    // --- C++ calling JS module functions via proxy ---
    {
        double result = ctx["math2"]["add"](10.0, 20.0).get<double>();
        std::cout << "[C++] math2.add(10, 20) = " << result << "\n";

        double sinVal = ctx["math2"]["trig"]["sin"](1.5708).get<double>();
        std::cout << "[C++] math2.trig.sin(PI/2) = " << sinVal << "\n";

        JsValue piVal = ctx["math2"]["PI"].get();
        std::cout << "[C++] math2.PI = " << piVal.toDouble() << "\n";
    }

    // --- Run the JS test script ---
    std::string scriptDir = argc > 1 ? argv[1] : "examples/scripts";
    return runScriptFile(ctx, scriptDir + "/test_module.js");
}
