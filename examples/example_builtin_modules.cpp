/**
 * @file example_builtin_modules.cpp
 * @brief Demonstrates registering QuickJS built-in modules (std, os, bjson).
 *
 * Features:
 * - addStdModule() / addOsModule() / addBjsonModule() for individual modules
 * - addBuiltinModules() for all at once
 */

#include "common.hpp"

int main(int argc, char* argv[]) {
    JsRuntime rt;
    rt.enableModuleLoader();

    JsContext ctx(rt);

    // Register all built-in modules at once
    ctx.addBuiltinModules();

    registerPrint(ctx);

    // --- Run the JS test script ---
    std::string scriptDir = argc > 1 ? argv[1] : "examples/scripts";
    return runScriptFile(ctx, scriptDir + "/test_builtin_modules.js");
}
