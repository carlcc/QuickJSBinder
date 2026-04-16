/**
 * @file example_proxy.cpp
 * @brief Demonstrates JsProxy (sol2-like) features.
 *
 * Covers:
 * - ctx["key"] = value (global property assignment)
 * - ctx["key"] = lambda (function registration)
 * - Chained property access ctx["a"]["b"]["c"]
 * - Function calls via proxy ctx["fn"](args...)
 * - Value retrieval via proxy .get<T>() and .get()
 */

#include "common.hpp"

int main(int argc, char* argv[]) {
    JsRuntime rt;
    JsContext ctx(rt);
    registerPrint(ctx);

    // --- Register functions and values via JsProxy ---
    ctx["add"] = [](double a, double b) { return a + b; };
    ctx["mul"] = [](double a, double b) { return a * b; };
    ctx["greeting"] = std::string("Hello from C++");
    ctx["magicNumber"] = 42;

    // --- Nested object via proxy ---
    ctx["config"] = JsValue::adopt(ctx, JS_NewObject(ctx));
    ctx["config"]["debug"] = true;
    ctx["config"]["version"] = std::string("1.0.0");
    ctx["config"]["maxRetries"] = 3;

    // --- C++ side: call JS functions via proxy ---
    {
        double sum = ctx["add"](10.0, 20.0).get<double>();
        std::cout << "[C++] add(10, 20) = " << sum << "\n";

        double product = ctx["mul"](6.0, 7.0).get<double>();
        std::cout << "[C++] mul(6, 7) = " << product << "\n";

        std::string g = ctx["greeting"].get<std::string>();
        std::cout << "[C++] greeting = " << g << "\n";

        int magic = ctx["magicNumber"].get<int>();
        std::cout << "[C++] magicNumber = " << magic << "\n";

        bool debug = ctx["config"]["debug"].get<bool>();
        std::cout << "[C++] config.debug = " << std::boolalpha << debug << "\n";
    }

    // --- JsValue::operator[] — proxy on any JsValue ---
    {
        JsValue global = ctx.globalObject();
        // Use operator[] on JsValue for the same sol2-like syntax
        double sum = global["add"](3.0, 7.0).get<double>();
        std::cout << "[C++] global[\"add\"](3, 7) = " << sum << "\n";

        std::string ver = global["config"]["version"].get<std::string>();
        std::cout << "[C++] global[\"config\"][\"version\"] = " << ver << "\n";

        // Chain deeper: get the config object, then access via proxy
        JsValue configVal = global.getProperty("config");
        bool debugFlag = configVal["debug"].get<bool>();
        std::cout << "[C++] configVal[\"debug\"] = " << std::boolalpha << debugFlag << "\n";

        // Set a new property via JsValue proxy
        configVal["newFlag"] = true;
        bool nf = ctx["config"]["newFlag"].get<bool>();
        std::cout << "[C++] config.newFlag (set via JsValue proxy) = "
                  << std::boolalpha << nf << "\n";
    }

    // --- Run the JS test script ---
    std::string scriptDir = argc > 1 ? argv[1] : "examples/scripts";
    return runScriptFile(ctx, scriptDir + "/test_proxy.js");
}
