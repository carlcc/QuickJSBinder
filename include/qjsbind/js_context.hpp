/**
 * @file js_context.hpp
 * @brief RAII wrapper for QuickJS JSContext*.
 *
 * JsContext automatically manages a JSContext lifetime:
 * - Constructor calls JS_NewContext.
 * - Destructor calls JS_FreeContext.
 * - Non-copyable, movable.
 * - Provides eval(), globalObject(), getModule(), module(), and operator[].
 *
 * @note Part of the QuickJSBinder header-only C++17 library.
 */
#pragma once

#include <cstddef>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include "js_value.hpp"

extern "C" {
#include "quickjs-libc.h"
}

namespace qjsbind {

// Forward declaration for JsProxy (defined in js_proxy.hpp).
class JsProxy;

// ============================================================================
// JsContext — RAII wrapper for JSContext*
// ============================================================================

/**
 * @brief Lightweight RAII wrapper for a QuickJS context.
 *
 * Automatically calls JS_FreeContext on destruction.
 * Non-copyable, movable.
 */
class JsContext {
public:
    /**
     * @brief Create a new context for the given runtime.
     * @param rt The runtime (must outlive this context).
     */
    explicit JsContext(JSRuntime* rt) : ctx_(JS_NewContext(rt)) {}

    ~JsContext() {
        if (ctx_) JS_FreeContext(ctx_);
    }

    JsContext(const JsContext&) = delete;
    JsContext& operator=(const JsContext&) = delete;

    JsContext(JsContext&& other) noexcept : ctx_(other.ctx_) { other.ctx_ = nullptr; }
    JsContext& operator=(JsContext&& other) noexcept {
        if (this != &other) {
            if (ctx_) JS_FreeContext(ctx_);
            ctx_ = other.ctx_;
            other.ctx_ = nullptr;
        }
        return *this;
    }

    /** @brief Get the raw JSContext pointer. */
    [[nodiscard]] JSContext* get() const noexcept { return ctx_; }

    /** @brief Implicit conversion to JSContext*. */
    operator JSContext*() const noexcept { return ctx_; }

    /**
     * @brief Evaluate a script string.
     *
     * @param code The script source (ASCII or UTF-8, null-terminated).
     * @param filename Logical filename for error messages.
     * @param flags JS_EVAL_TYPE_GLOBAL by default.
     * @return JsValue wrapping the evaluation result.
     */
    [[nodiscard]] JsValue eval(const char* code, const char* filename = "<eval>",
                               int flags = JS_EVAL_TYPE_GLOBAL) const {
        return JsValue::adopt(ctx_,
            JS_Eval(ctx_, code, strlen(code), filename, flags));
    }

    /**
     * @brief Get the global object.
     * @return JsValue wrapping the global object (owned).
     */
    [[nodiscard]] JsValue globalObject() const {
        return JsValue::adopt(ctx_, JS_GetGlobalObject(ctx_));
    }

    [[nodiscard]] JsValue getExceptionMessage() const
    {
        return JsValue::adopt(ctx_, JS_GetException(ctx_));
    }

    /**
     * @brief Access a global property via proxy, enabling sol2-like syntax.
     *
     * Returns a JsProxy that supports chained access, assignment, and function calls:
     * @code
     * ctx["math"]["add"] = [](double a, double b) { return a + b; };
     * int result = ctx["math"]["add"](1, 2).get<int>();
     * @endcode
     *
     * @param key Global property name.
     * @return A JsProxy for the global property.
     *
     * @note Defined in js_proxy.hpp (include qjsbind.hpp for full functionality).
     */
    inline JsProxy operator[](const char* key);

    /**
     * @brief Get a named module (global property) as a JsValue.
     *
     * This retrieves a previously-installed module by its global name.
     * Useful for game engines and dynamic scripting scenarios where you
     * need to reference a module after installation:
     *
     * @code
     * JsModule engine(ctx, "Engine");
     * engine.function("log", &my_log);
     * engine.install();
     *
     * // Later, retrieve the module:
     * JsValue engineMod = ctx.getModule("Engine");
     * engineMod["fps"] = 60.0;
     * std::string ver = engineMod["version"].get<std::string>();
     * @endcode
     *
     * @param name The global property name of the module.
     * @return JsValue wrapping the module object (owned). Returns
     *         JS_UNDEFINED if the module does not exist.
     */
    [[nodiscard]] JsValue getModule(const char* name) const {
        JSValue global = JS_GetGlobalObject(ctx_);
        JSValue mod = JS_GetPropertyStr(ctx_, global, name);
        JS_FreeValue(ctx_, global);
        return JsValue::adopt(ctx_, mod);
    }

    /**
     * @brief Get a named module as a JsProxy for chained access.
     *
     * Combines getModule() with proxy access for a more fluent API:
     *
     * @code
     * // Retrieve module and use it via proxy
     * auto engine = ctx.module("Engine");
     * engine["fps"] = 60.0;
     * engine["log"]("dynamic call");
     *
     * // Chain deeper
     * double pi = ctx.module("math2")["PI"].get<double>();
     * @endcode
     *
     * @param name The global property name of the module.
     * @return A JsProxy wrapping the module object.
     *
     * @note Defined after JsProxy is fully declared (in js_proxy.hpp).
     */
    inline JsProxy module(const char* name);

    // -----------------------------------------------------------------------
    // File evaluation
    // -----------------------------------------------------------------------

    /**
     * @brief Evaluate a JS file, automatically detecting whether it is an ES module.
     *
     * This method reads a file and evaluates it. If the source code contains
     * ES module syntax (`import`/`export`), it is automatically evaluated as a
     * module (using JS_EVAL_TYPE_MODULE). Otherwise, it is evaluated as a
     * global script.
     *
     * For module files, the module's dependencies are resolved and the module
     * is fully evaluated (including setting import.meta.url).
     *
     * @note If the module uses top-level `await`, the returned JsValue is a
     *       **Promise**. The caller is responsible for settling it — either by
     *       calling `await()` for synchronous blocking, or by driving the
     *       event loop with `loopOnce()` / `pollIO()` in a game-loop style.
     *
     * @code
     * JsRuntime rt;
     * rt.enableModuleLoader();
     * JsContext ctx(rt);
     *
     * JsValue result = ctx.evalFile("examples/scripts/my_module.js");
     * if (result.isException()) { ctx.dumpError(); }
     *
     * // If the module has top-level await, result is a Promise.
     * // Option A: block until settled (CLI / tool use)
     * //   result = ctx.await(std::move(result));
     * // Option B: drive per-frame (game engine use)
     * //   while (running) { ctx.pollIO(0); ctx.loopOnce(); render(); }
     * @endcode
     *
     * @param filename Path to the JS file.
     * @return JsValue wrapping the evaluation result (module namespace for
     *         modules, last expression value for scripts). For modules with
     *         top-level await, returns an **unsettled Promise**. Returns an
     *         exception marker on error.
     */
    [[nodiscard]] JsValue evalFile(const char* filename, JsValue* moduleValue = nullptr) const {
        // Read file contents.
        std::ifstream ifs(filename);
        if (!ifs.is_open()) {
            JS_ThrowReferenceError(ctx_, "could not open file: %s", filename);
            return JsValue::adopt(ctx_, JS_EXCEPTION);
        }
        std::ostringstream oss;
        oss << ifs.rdbuf();
        std::string code = oss.str();

        // Detect if this is an ES module.
        bool isModule = JS_DetectModule(code.c_str(), code.size());
        int flags = isModule ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;

        if (isModule) {
            // Compile the module first.
            JSValue compiled = JS_Eval(ctx_, code.c_str(), code.size(),
                                       filename,
                                       flags | JS_EVAL_FLAG_COMPILE_ONLY);
            if (JS_IsException(compiled)) {
                return JsValue::adopt(ctx_, compiled);
            }

            // Set import.meta (url, etc.).
            js_module_set_import_meta(ctx_, compiled, true, true);

            if (moduleValue != nullptr) {
                *moduleValue = JsValue::dup(ctx_, compiled);
            }
            // Evaluate (resolve + execute) the module.
            JSValue result = JS_EvalFunction(ctx_, compiled);
            result = js_std_await(ctx_, result);
            // compiled is consumed by JS_EvalFunction, no need to free.
            return JsValue::adopt(ctx_, result);
        } else {
            return JsValue::adopt(ctx_,
                JS_Eval(ctx_, code.c_str(), code.size(), filename, flags));
        }
    }

    /**
     * @brief Evaluate a string as an ES module.
     *
     * @note If the module uses top-level `await`, the returned JsValue is a
     *       **Promise**. The caller is responsible for settling it — either
     *       via `await()` or by driving the event loop manually.
     *
     * @param code Module source code.
     * @param filename Logical filename for import resolution and error messages.
     * @return JsValue wrapping the evaluation result. For modules with
     *         top-level await, returns an unsettled Promise. Returns an
     *         exception marker on error.
     */
    [[nodiscard]] JsValue evalModule(const char* code, JsValue* moduleValue, const char* filename = "<module>") const {
        size_t len = strlen(code);
        JSValue compiled = JS_Eval(ctx_, code, len, filename,
                                   JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(compiled)) {
            return JsValue::adopt(ctx_, compiled);
        }
        js_module_set_import_meta(ctx_, compiled, true,
                                  true /* is_main */);
        if (moduleValue != nullptr) {
            *moduleValue = JsValue::dup(ctx_, compiled);
        }
        JSValue result = JS_EvalFunction(ctx_, compiled);
        result = js_std_await(ctx_, result);
        return JsValue::adopt(ctx_, result);
    }

    [[nodiscard]] JsValue evalModuleFile(const char* filename, JsValue* moduleValue = nullptr)
    {
        std::string code;
        {
            std::ifstream ifs(filename);
            if (!ifs.is_open()) {
                JS_ThrowReferenceError(ctx_, "could not open file: %s", filename);
                return JsValue::adopt(ctx_, JS_EXCEPTION);
            }
            std::ostringstream oss;
            oss << ifs.rdbuf();
            code = oss.str();
        }
        return evalModule(code.c_str(), moduleValue, filename);
    }

    // -----------------------------------------------------------------------
    // Built-in module registration
    // -----------------------------------------------------------------------

    /**
     * @brief Register the QuickJS "std" built-in module.
     *
     * The `std` module provides standard I/O, file operations, and other
     * system-level utilities:
     * - `std.printf`, `std.puts`, `std.getline`
     * - `std.open`, `std.fdopen`, `std.popen`
     * - `std.loadFile`, `std.strerror`
     * - `std.exit`, `std.gc`
     * - `std.getenv`, `std.setenv`, `std.unsetenv`
     * - `std.urlGet`
     *
     * @code
     * JsContext ctx(rt);
     * ctx.addStdModule();
     * ctx.evalModule("import * as std from 'std'; std.puts('hello');");
     * @endcode
     *
     * @param module_name Custom module name (default: "std").
     * @return Reference to this JsContext for chaining.
     */
    JsContext& addStdModule(const char* module_name = "std") {
        js_init_module_std(ctx_, module_name);
        return *this;
    }

    /**
     * @brief Register the QuickJS "os" built-in module.
     *
     * The `os` module provides operating system interfaces:
     * - `os.open`, `os.close`, `os.read`, `os.write`, `os.seek`
     * - `os.remove`, `os.rename`, `os.realpath`, `os.getcwd`, `os.chdir`
     * - `os.mkdir`, `os.readdir`, `os.stat`, `os.lstat`
     * - `os.exec`, `os.waitpid`, `os.pipe`, `os.kill`, `os.signal`
     * - `os.setTimeout`, `os.clearTimeout`, `os.sleep`
     * - `os.platform`
     *
     * @code
     * JsContext ctx(rt);
     * ctx.addOsModule();
     * ctx.evalModule("import * as os from 'os'; os.chdir('/tmp');");
     * @endcode
     *
     * @param module_name Custom module name (default: "os").
     * @return Reference to this JsContext for chaining.
     */
    JsContext& addOsModule(const char* module_name = "os") {
        js_init_module_os(ctx_, module_name);
        return *this;
    }

    /**
     * @brief Register the QuickJS "bjson" built-in module.
     *
     * The `bjson` module provides binary JSON (de)serialization:
     * - `bjson.write(obj)` — serialize a JS value to an ArrayBuffer.
     * - `bjson.read(buf)` — deserialize an ArrayBuffer to a JS value.
     *
     * @code
     * JsContext ctx(rt);
     * ctx.addBjsonModule();
     * ctx.evalModule(R"(
     *     import * as bjson from 'bjson';
     *     let buf = bjson.write({x: 1, y: 2});
     *     let obj = bjson.read(buf);
     * )");
     * @endcode
     *
     * @param module_name Custom module name (default: "bjson").
     * @return Reference to this JsContext for chaining.
     */
    JsContext& addBjsonModule(const char* module_name = "bjson") {
        js_init_module_bjson(ctx_, module_name);
        return *this;
    }

    JsContext& addHelpers(int argc, char** argv) {
        js_std_add_helpers(ctx_, 0, argv);
        return *this;
    }

    /**
     * @brief Register all QuickJS built-in modules (std, os, bjson) and helpers.
     *
     * Convenience method that registers all three built-in modules and helpers at once.
     * Also installs the standard helpers (console.log, print, etc.) for
     * compatibility with the QuickJS REPL environment.
     *
     * @code
     * JsRuntime rt;
     * rt.enableModuleLoader();
     * JsContext ctx(rt);
     * ctx.addBuiltinModules();
     *
     * ctx.evalModule(R"(
     *     import * as std from 'std';
     *     import * as os from 'os';
     *     std.puts('platform: ' + os.platform);
     * )");
     * @endcode
     *
     * @param argc the command line args count
     * @param argv the command line args
     * @return Reference to this JsContext for chaining.
     */
    JsContext& addBuiltinModules(int argc = 0, char** argv = nullptr) {
        addStdModule();
        addOsModule();
        addBjsonModule();
        addHelpers(argc, argv);
        return *this;
    }

    // -----------------------------------------------------------------------
    // Event loop & async support
    // -----------------------------------------------------------------------

    /**
     * @brief Run the standard event loop until all pending jobs complete.
     *
     * This is the main event loop driver. It repeatedly executes pending
     * JS jobs (microtasks, async continuations, timers, I/O callbacks)
     * until there are no more pending tasks. Use this after evaluating
     * an async script or a module that uses `import()`, `setTimeout`,
     * promises, top-level `await`, etc.
     *
     * **Typical usage pattern:**
     * @code
     * JsRuntime rt;
     * rt.enableModuleLoader();       // required for std/os handlers
     * JsContext ctx(rt);
     * ctx.addBuiltinModules();
     *
     * // Evaluate an async module
     * ctx.evalModule(R"(
     *     import * as os from 'os';
     *     os.setTimeout(() => { console.log("timer fired"); }, 100);
     *     let p = new Promise(resolve => {
     *         os.setTimeout(() => resolve(42), 200);
     *     });
     *     let result = await p;
     *     console.log("result:", result);
     * )");
     *
     * // Drive the event loop — executes timers, resolves promises, etc.
     * int err = ctx.loop();
     * if (err) { ctx.dumpError(); }
     * @endcode
     *
     * @pre `JsRuntime::enableModuleLoader()` must have been called on the
     *      parent runtime to initialize the std event handlers. Without it,
     *      the loop will not process os.setTimeout / I/O callbacks.
     *
     * @return 0 on success, non-zero if an unhandled exception occurred
     *         during job execution.
     *
     * @note This is a **blocking** call. It returns only when all pending
     *       jobs and I/O events have been processed. For integration into
     *       an external event loop (e.g. a game engine main loop), use
     *       `loopOnce()` or `pollIO()` instead.
     *
     * @see loopOnce(), pollIO()
     */
    int loop() const {
        return js_std_loop(ctx_);
    }

    /**
     * @brief Execute one iteration of the event loop.
     *
     * Processes at most one pending job (microtask / timer / I/O callback)
     * and returns immediately. This is designed for **integration into an
     * external event loop** where you cannot hand control entirely to
     * QuickJS.
     *
     * **Game engine integration example:**
     * @code
     * // In your main game loop:
     * while (gameRunning) {
     *     processInput();
     *     updatePhysics(dt);
     *
     *     // Pump JS event loop — non-blocking, processes one pending job
     *     ctx.loopOnce();
     *
     *     renderFrame();
     * }
     * @endcode
     *
     * **Drain-all pattern (equivalent to loop()):**
     * @code
     * // Keep calling loopOnce() until no more pending work
     * while (ctx.loopOnce() == 0) {
     *     // still has pending jobs, keep going
     * }
     * @endcode
     *
     * @pre `JsRuntime::enableModuleLoader()` must have been called.
     *
     * @return 0 if there are still pending jobs remaining after this
     *         iteration; non-zero when there are no more pending jobs
     *         or an error occurred.
     *
     * @see loop(), pollIO()
     */
    int loopOnce() const {
        return js_std_loop_once(ctx_);
    }

    /**
     * @brief Poll for I/O events with a timeout.
     *
     * Waits for I/O readiness on file descriptors registered by the `os`
     * module (sockets, pipes, etc.) and fires any ready callbacks. This
     * is useful when you need fine-grained control over blocking and I/O
     * in a custom event loop.
     *
     * **Timeout semantics:**
     * - `timeout_ms > 0`: Block for at most `timeout_ms` milliseconds.
     * - `timeout_ms == 0`: Non-blocking poll; returns immediately.
     * - `timeout_ms < 0`: Block indefinitely until an I/O event occurs.
     *
     * **Custom event loop with I/O:**
     * @code
     * // Evaluate a script that sets up async I/O (e.g., os.setTimeout)
     * ctx.evalModule(R"(
     *     import * as os from 'os';
     *     os.setTimeout(() => console.log("hello"), 500);
     * )");
     *
     * // Custom event loop with 16ms polling (≈60fps)
     * bool done = false;
     * while (!done) {
     *     // Poll I/O with 16ms timeout
     *     ctx.pollIO(16);
     *
     *     // Process any ready JS jobs
     *     int rc = ctx.loopOnce();
     *     if (rc != 0) done = true;  // no more pending work
     *
     *     // ... do other work (rendering, physics, etc.)
     * }
     * @endcode
     *
     * @pre `JsRuntime::enableModuleLoader()` must have been called.
     *
     * @param timeout_ms Timeout in milliseconds (0 = non-blocking,
     *                   negative = block forever).
     * @return 0 on success, non-zero on error.
     *
     * @see loopOnce(), loop()
     */
    int pollIO(int timeout_ms) const {
        return js_std_poll_io(ctx_, timeout_ms);
    }

    /**
     * @brief Synchronously await a JS Promise value.
     *
     * Takes a JSValue that may be a Promise and runs the event loop until
     * that Promise settles (fulfills or rejects). Returns the resolved
     * value or the exception.
     *
     * This is the C++ equivalent of `await` in JS — it blocks until the
     * async operation completes.
     *
     * **Usage pattern:**
     * @code
     * // Evaluate an expression that returns a Promise
     * JsValue promise = ctx.eval("fetch('/api/data')");
     *
     * // Await the result synchronously
     * JsValue result = ctx.await(std::move(promise));
     * if (result.isException()) {
     *     // Promise rejected or error during await
     *     ctx.dumpError();
     * } else {
     *     // result contains the resolved value
     *     std::string data = result.get<std::string>();
     * }
     * @endcode
     *
     * **With evalModule (top-level await):**
     * @code
     * // ES modules with top-level await return a Promise
     * JsValue moduleResult = ctx.evalModule(R"(
     *     const response = await fetch('/api');
     *     export default response;
     * )");
     * JsValue resolved = ctx.await(std::move(moduleResult));
     * @endcode
     *
     * @pre `JsRuntime::enableModuleLoader()` must have been called.
     *
     * @param val The JsValue to await. Ownership is transferred — the
     *            input JsValue is consumed (moved from). If `val` is not
     *            a Promise, it is returned as-is without entering the
     *            event loop.
     * @return A new JsValue containing the resolved value, or an
     *         exception marker if the Promise was rejected.
     *
     * @note This consumes the input value. After calling await(), the
     *       original `val` is in a moved-from state and should not be used.
     *
     * @see loop(), loopOnce()
     */
    JsValue await(JsValue val) const {
        JSValue result = js_std_await(ctx_, val.release());
        return JsValue::adopt(ctx_, result);
    }

    /**
     * @brief Dump the current pending exception to stderr.
     *
     * Prints the exception message and stack trace of the most recent
     * unhandled exception. Useful for debugging after `eval()`, `loop()`,
     * or `await()` returns an error or exception.
     *
     * @code
     * JsValue result = ctx.eval("throw new Error('oops')");
     * if (result.isException()) {
     *     ctx.dumpError();  // prints "Error: oops" + stack trace to stderr
     * }
     * @endcode
     */
    void dumpError() const {
        js_std_dump_error(ctx_);
    }

private:
    JSContext* ctx_;
};

} // namespace qjsbind
