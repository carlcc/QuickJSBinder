/**
 * @file js_context.hpp
 * @brief Two-layer wrapper for QuickJS JSContext*.
 *
 * - **JsContextView** (base): non-owning view over a JSContext*.
 *   Provides eval(), globalObject(), evalFile(), event-loop helpers, etc.
 *   Useful when you only have a borrowed JSContext* (e.g., inside a callback).
 *
 * - **JsContext** (derived): RAII owner that calls JS_NewContext / JS_FreeContext.
 *   Inherits every method from JsContextView while managing the lifetime.
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
// JsContextView — non-owning wrapper (base class)
// ============================================================================

/**
 * @brief Non-owning view over a JSContext*.
 *
 * Does NOT manage the lifetime of the underlying JSContext.
 * Provides the full set of helper methods (eval, evalFile, loop, …)
 * so that code holding only a raw JSContext* can use the same API.
 *
 * Copyable (trivially — just copies the pointer).
 */
class JsContextView {
public:
    /// Construct from a raw JSContext* (borrowed, caller manages lifetime).
    explicit JsContextView(JSContext* ctx) noexcept : ctx_(ctx) {}

    JsContextView(const JsContextView&) = default;
    JsContextView& operator=(const JsContextView&) = default;

    /** @brief Get the raw JSContext pointer. */
    [[nodiscard]] JSContext* get() const noexcept { return ctx_; }

    /** @brief Implicit conversion to JSContext*. */
    operator JSContext*() const noexcept { return ctx_; }

    // -----------------------------------------------------------------------
    // Eval
    // -----------------------------------------------------------------------

    /**
     * @brief Evaluate a script string.
     *
     * @param code  Script source (ASCII / UTF-8, null-terminated).
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

    /**
     * @brief Create an empty JS object.
     * @return JsValue wrapping the new object (owned).
     */
    [[nodiscard]] JsValue createObject() const {
        return JsValue::adopt(ctx_, JS_NewObject(ctx_));
    }

    [[nodiscard]] JsValue getExceptionMessage() const {
        return JsValue::adopt(ctx_, JS_GetException(ctx_));
    }

    /**
     * @brief Access a global property via proxy, enabling sol2-like syntax.
     *
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
     * @code
     * JsValue engineMod = ctx.getModule("Engine");
     * @endcode
     *
     * @param name The global property name.
     * @return JsValue wrapping the module object (owned). JS_UNDEFINED if absent.
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
     * @code
     * auto engine = ctx.module("Engine");
     * engine["fps"] = 60.0;
     * @endcode
     *
     * @note Defined after JsProxy is fully declared (in js_proxy.hpp).
     */
    inline JsProxy module(const char* name);

    // -----------------------------------------------------------------------
    // File evaluation
    // -----------------------------------------------------------------------

    /**
     * @brief Evaluate a JS file, auto-detecting ES module syntax.
     *
     * @param filename Path to the JS file.
     * @param moduleValue Optional out-param receiving the compiled module object.
     * @return JsValue wrapping the evaluation result.
     */
    [[nodiscard]] JsValue evalFile(const char* filename, JsValue* moduleValue = nullptr) const {
        std::ifstream ifs(filename);
        if (!ifs.is_open()) {
            JS_ThrowReferenceError(ctx_, "could not open file: %s", filename);
            return JsValue::adopt(ctx_, JS_EXCEPTION);
        }
        std::ostringstream oss;
        oss << ifs.rdbuf();
        std::string code = oss.str();

        bool isModule = JS_DetectModule(code.c_str(), code.size());
        int flags = isModule ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;

        if (isModule) {
            JSValue compiled = JS_Eval(ctx_, code.c_str(), code.size(),
                                       filename,
                                       flags | JS_EVAL_FLAG_COMPILE_ONLY);
            if (JS_IsException(compiled)) {
                return JsValue::adopt(ctx_, compiled);
            }
            js_module_set_import_meta(ctx_, compiled, true, true);
            if (moduleValue != nullptr) {
                *moduleValue = JsValue::dup(ctx_, compiled);
            }
            JSValue result = JS_EvalFunction(ctx_, compiled);
            result = js_std_await(ctx_, result);
            return JsValue::adopt(ctx_, result);
        } else {
            return JsValue::adopt(ctx_,
                JS_Eval(ctx_, code.c_str(), code.size(), filename, flags));
        }
    }

    /**
     * @brief Evaluate a string as an ES module.
     *
     * @param code   Module source code.
     * @param moduleValue Optional out-param.
     * @param filename Logical filename for error messages.
     * @return JsValue wrapping the evaluation result.
     */
    [[nodiscard]] JsValue evalModule(const char* code, JsValue* moduleValue, const char* filename = "<module>") const {
        size_t len = strlen(code);
        JSValue compiled = JS_Eval(ctx_, code, len, filename,
                                   JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(compiled)) {
            return JsValue::adopt(ctx_, compiled);
        }
        js_module_set_import_meta(ctx_, compiled, true, true);
        if (moduleValue != nullptr) {
            *moduleValue = JsValue::dup(ctx_, compiled);
        }
        JSValue result = JS_EvalFunction(ctx_, compiled);
        result = js_std_await(ctx_, result);
        return JsValue::adopt(ctx_, result);
    }

    [[nodiscard]] JsValue evalModuleFile(const char* filename, JsValue* moduleValue = nullptr) {
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

    JsContextView& addStdModule(const char* module_name = "std") {
        js_init_module_std(ctx_, module_name);
        return *this;
    }

    JsContextView& addOsModule(const char* module_name = "os") {
        js_init_module_os(ctx_, module_name);
        return *this;
    }

    JsContextView& addBjsonModule(const char* module_name = "bjson") {
        js_init_module_bjson(ctx_, module_name);
        return *this;
    }

    JsContextView& addHelpers(int argc = 0, char** argv = nullptr) {
        js_std_add_helpers(ctx_, 0, argv);
        return *this;
    }

    JsContextView& addBuiltinModules(int argc = 0, char** argv = nullptr) {
        addStdModule();
        addOsModule();
        addBjsonModule();
        addHelpers(argc, argv);
        return *this;
    }

    // -----------------------------------------------------------------------
    // Event loop & async support
    // -----------------------------------------------------------------------

    /** @brief Run the standard event loop until all pending jobs complete. */
    int loop() const {
        return js_std_loop(ctx_);
    }

    /** @brief Execute one iteration of the event loop. */
    int loopOnce() const {
        return js_std_loop_once(ctx_);
    }

    /** @brief Poll for I/O events with a timeout (ms). */
    int pollIO(int timeout_ms) const {
        return js_std_poll_io(ctx_, timeout_ms);
    }

    /** @brief Synchronously await a JS Promise value. */
    JsValue await(JsValue val) const {
        JSValue result = js_std_await(ctx_, val.release());
        return JsValue::adopt(ctx_, result);
    }

    /** @brief Dump the current pending exception to stderr. */
    void dumpError() const {
        js_std_dump_error(ctx_);
    }

protected:
    JSContext* ctx_;
};

// ============================================================================
// JsContext — RAII owning wrapper (derived)
// ============================================================================

/**
 * @brief RAII wrapper for a QuickJS context.
 *
 * Inherits every method from JsContextView. Additionally:
 * - Constructor calls JS_NewContext.
 * - Destructor calls JS_FreeContext.
 * - Non-copyable, movable.
 */
class JsContext : public JsContextView {
public:
    /** @brief Create a new context for the given runtime. */
    explicit JsContext(JSRuntime* rt) : JsContextView(JS_NewContext(rt)) {}

    ~JsContext() {
        if (ctx_) JS_FreeContext(ctx_);
    }

    JsContext(const JsContext&) = delete;
    JsContext& operator=(const JsContext&) = delete;

    JsContext(JsContext&& other) noexcept : JsContextView(other.ctx_) {
        other.ctx_ = nullptr;
    }

    JsContext& operator=(JsContext&& other) noexcept {
        if (this != &other) {
            if (ctx_) JS_FreeContext(ctx_);
            ctx_ = other.ctx_;
            other.ctx_ = nullptr;
        }
        return *this;
    }
};

} // namespace qjsbind
