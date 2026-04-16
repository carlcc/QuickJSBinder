/**
 * @file example_native_module.cpp
 * @brief Demonstrates loading a native shared library (.so/.dylib) as an ES module.
 *
 * Covers:
 * - JsRuntime::enableModuleLoader() — required for dynamic .so loading
 * - JsContext::evalFile() — loads a JS file that imports from a native .so
 * - Native plugin exports: functions, constants, and C++ classes via qjsbind
 *
 * The native plugin (native_plugin.so / native_plugin.dylib) is built as a
 * separate shared library target. QuickJS's built-in module loader handles
 * the dlopen/dlsym automatically.
 *
 * Build & Run:
 *   xmake f --qjsb_build_examples=y
 *   xmake build
 *   xmake run example_native_module
 */

#include "common.hpp"

#include <filesystem>

int main(int argc, char* argv[]) {
    JsRuntime rt;
    rt.enableModuleLoader();

    JsContext ctx(rt);
    registerPrint(ctx);

    std::cout << "=== Native Module (shared library) Example ===\n\n";

    // The JS test script lives in examples/scripts/.
    // It does: import { ... } from "./native_plugin.so"
    //
    // QuickJS resolves the import relative to the importing file's path.
    // We need to make sure the .so is next to the JS file, or we adjust the path.
    //
    // Strategy: copy/symlink the built .so into examples/scripts/ at build time,
    // OR change directory so that "./" resolves correctly.
    // For simplicity, we'll create a symlink or just chdir.

    std::string scriptDir = argc > 1 ? argv[1] : "examples/native_scripts";

    // Ensure the native plugin .so is accessible from the script directory.
    // The build system places it in build/*/... but we symlink it.
    // (The xmake.lua handles copying the .so to the scripts dir via after_build.)

    std::string mainModule = scriptDir + "/test_native_module.js";
    std::cout << "[C++] Loading JS module: " << mainModule << "\n";
    std::cout << "[C++] The JS file imports from a native .so plugin.\n\n";

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

    std::cout << "\n=== Native Module Example Done ===\n";
    return 0;
}
