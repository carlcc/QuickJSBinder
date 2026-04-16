/**
 * @file js_runtime.hpp
 * @brief RAII wrapper for QuickJS JSRuntime*.
 *
 * JsRuntime automatically manages a JSRuntime lifetime:
 * - Constructor calls JS_NewRuntime.
 * - Destructor calls JS_FreeRuntime.
 * - Non-copyable, movable.
 *
 * @note Part of the QuickJSBinder header-only C++17 library.
 */
#pragma once

#include <cstddef>
#include <utility>

extern "C" {
#include "quickjs.h"
#include "quickjs-libc.h"
}

namespace qjsbind {

// ============================================================================
// JsRuntime — RAII wrapper for JSRuntime*
// ============================================================================

/**
 * @brief Lightweight RAII wrapper for a QuickJS runtime.
 *
 * Automatically calls JS_FreeRuntime on destruction.
 * Non-copyable, movable.
 */
class JsRuntime {
public:
    /** @brief Create a new QuickJS runtime. */
    JsRuntime() : rt_(JS_NewRuntime()), module_loader_enabled_(false) {}

    ~JsRuntime() {
        if (rt_) {
            if (module_loader_enabled_) {
                js_std_free_handlers(rt_);
            }
            JS_FreeRuntime(rt_);
        }
    }

    JsRuntime(const JsRuntime&) = delete;
    JsRuntime& operator=(const JsRuntime&) = delete;

    JsRuntime(JsRuntime&& other) noexcept
        : rt_(other.rt_), module_loader_enabled_(other.module_loader_enabled_) {
        other.rt_ = nullptr;
        other.module_loader_enabled_ = false;
    }
    JsRuntime& operator=(JsRuntime&& other) noexcept {
        if (this != &other) {
            if (rt_) {
                if (module_loader_enabled_) js_std_free_handlers(rt_);
                JS_FreeRuntime(rt_);
            }
            rt_ = other.rt_;
            module_loader_enabled_ = other.module_loader_enabled_;
            other.rt_ = nullptr;
            other.module_loader_enabled_ = false;
        }
        return *this;
    }

    /** @brief Get the raw JSRuntime pointer. */
    [[nodiscard]] JSRuntime* get() const noexcept { return rt_; }

    /** @brief Implicit conversion to JSRuntime*. */
    operator JSRuntime*() const noexcept { return rt_; }

    /**
     * @brief Enable the built-in module loader for ES module support.
     *
     * This must be called before evaluating any script that uses `import`.
     * It sets up:
     * - The standard module normalizer/loader (file-based).
     * - The std event handlers required for promises/async modules.
     *
     * @code
     * JsRuntime rt;
     * rt.enableModuleLoader();   // call once, before creating contexts
     *
     * JsContext ctx(rt);
     * ctx.evalFile("main.js");   // can now use `import ... from ...`
     * @endcode
     *
     * @note This is idempotent — calling it multiple times is safe.
     */
    void enableModuleLoader() {
        if (!module_loader_enabled_) {
            js_std_init_handlers(rt_);
            JS_SetModuleLoaderFunc2(rt_, nullptr, js_module_loader, nullptr, nullptr);
            module_loader_enabled_ = true;
        }
    }

private:
    JSRuntime* rt_;
    bool module_loader_enabled_;
};

} // namespace qjsbind
