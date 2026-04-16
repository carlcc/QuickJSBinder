/**
 * @file example_es_module.cpp
 * @brief Demonstrates loading JS files as ES modules with import/export.
 *
 * Covers:
 * - JsRuntime::enableModuleLoader() — enable the built-in file-based module loader
 * - JsContext::evalFile() — load a JS file, auto-detecting module vs script
 * - JsContext::evalModule() — evaluate a string as an ES module
 * - C++ functions accessible from within ES modules
 * - Module import chains: test_es_module.js → math_utils.js, vec2.js
 */

#include "common.hpp"

int main(int argc, char* argv[]) {
    JsRuntime rt;
    // Enable the module loader so that `import ... from "..."` works.
    rt.enableModuleLoader();

    JsContext ctx(rt);
    registerPrint(ctx);

    // --- Register C++ functions that will be called from ES modules ---
    ctx["greet"] = [](std::string name) -> std::string {
        return "Hello, " + name + "!";
    };
    ctx["nativeAdd"] = [](double a, double b) { return a + b; };

    std::cout << "=== ES Module Example ===\n\n";

    // --- 1. Load a JS file as an ES module ---
    // evalFile() automatically detects whether the file uses import/export.
    std::string scriptDir = argc > 1 ? argv[1] : "examples/scripts";
    std::string mainModule = scriptDir + "/test_es_module.js";

    std::cout << "[C++] Loading ES module: " << mainModule << "\n\n";
    JsValue result = ctx.evalFile(mainModule.c_str());
    if (result.isException()) {
        JsValue exc = JsValue::adopt(ctx, JS_GetException(ctx));
        std::cerr << "JS Exception: " << exc.toString() << std::endl;
        JsValue stack = exc.getProperty("stack");
        if (!stack.isUndefined()) {
            std::cerr << stack.toString() << std::endl;
        }
        return 1;
    }

    // --- 2. Evaluate an inline ES module string ---
    std::cout << "\n[C++] Evaluating inline ES module...\n";
    const char* inlineModule = R"(
        import { add, PI } from "./math_utils.js";
        print("inline module: add(PI, 1) = " + add(PI, 1));
    )";
    // Note: evalModule needs a meaningful filename for import resolution.
    JsValue result2 = ctx.evalModule(inlineModule, nullptr,
                                      (scriptDir + "/<inline>").c_str());
    if (result2.isException()) {
        JsValue exc = JsValue::adopt(ctx, JS_GetException(ctx));
        std::cerr << "JS Exception: " << exc.toString() << std::endl;
        JsValue stack = exc.getProperty("stack");
        if (!stack.isUndefined()) {
            std::cerr << stack.toString() << std::endl;
        }
        return 1;
    }

    // --- 3. Load a regular (non-module) script via evalFile ---
    std::cout << "\n[C++] Loading regular script via evalFile...\n";
    JsValue result3 = ctx.evalFile((scriptDir + "/test_proxy.js").c_str());
    if (result3.isException()) {
        JsValue exc = JsValue::adopt(ctx, JS_GetException(ctx));
        std::cerr << "JS Exception: " << exc.toString() << std::endl;
        return 1;
    }

    std::cout << "\n=== ES Module Example Done ===\n";
    return 0;
}
