/**
 * @file js_native_module.hpp
 * @brief Helper for building native ES modules (shared libraries) with QuickJSBinder.
 *
 * QuickJS can dynamically load `.so` / `.dylib` / `.dll` files as ES modules.
 * The shared library must export a C function:
 *
 *     extern "C" JSModuleDef* js_init_module(JSContext* ctx, const char* module_name);
 *
 * This header provides:
 * - **NativeModuleExport**: a builder API that wraps the low-level
 *   JS_NewCModule / JS_AddModuleExport / JS_SetModuleExport calls.
 * - **QJSBIND_NATIVE_MODULE(setup_func)**: a macro that auto-generates
 *   `js_init_module`, automatically collecting export names so you never
 *   need to maintain a separate list.
 *
 * Usage (in a .cpp compiled as a shared library):
 * @code
 * #include <qjsbind/qjsbind.hpp>
 *
 * using namespace qjsbind;
 *
 * QJSBIND_DECLARE_CONVERTER(Vec3);
 *
 * static void setupModule(NativeModuleExport& mod) {
 *     mod.function("add", [](double a, double b) { return a + b; });
 *     mod.value("PI", 3.14159265358979);
 *     mod.class_<Vec3>("Vec3")
 *        .constructor<void(), void(double, double, double)>()
 *        .method("length", &Vec3::length)
 *        .installAsModuleExport(mod);
 * }
 *
 * QJSBIND_NATIVE_MODULE(setupModule)
 * @endcode
 *
 * @note Part of the QuickJSBinder header-only C++20 library.
 */
#pragma once

#include <cassert>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "js_value.hpp"
#include "js_converter.hpp"
#include "function_wrapper.hpp"
#include "class_binder.hpp"

namespace qjsbind {

// ============================================================================
// NativeModuleExport — builder for populating a native ES module
// ============================================================================

/**
 * @brief Builder for populating a QuickJS native (C) module with exports.
 *
 * Operates in two modes:
 * - **Dry-run** (ctx == nullptr, m == nullptr): only collects export names.
 * - **Real** (valid ctx and m): actually registers exports via JS_SetModuleExport.
 *
 * The QJSBIND_NATIVE_MODULE macro runs the setup function twice:
 * 1. Dry-run to collect export names → calls JS_AddModuleExport for each.
 * 2. Real run as the module init callback → calls JS_SetModuleExport for each.
 */
class NativeModuleExport {
public:
    /**
     * @brief Construct a module export builder.
     * @param ctx The JS context (nullptr for dry-run mode).
     * @param m The module definition (nullptr for dry-run mode).
     */
    NativeModuleExport(JSContext* ctx, JSModuleDef* m)
        : ctx_(ctx), m_(m), dry_run_(ctx == nullptr) {}

    // Non-copyable, non-movable (used on the stack in init callback).
    NativeModuleExport(const NativeModuleExport&) = delete;
    NativeModuleExport& operator=(const NativeModuleExport&) = delete;

    /// @brief Check if this builder is in dry-run (name-collecting) mode.
    [[nodiscard]] bool isDryRun() const noexcept { return dry_run_; }

    // -----------------------------------------------------------------------
    // Function export
    // -----------------------------------------------------------------------

    /**
     * @brief Export a function from this module.
     *
     * @code
     * mod.function("add", [](double a, double b) { return a + b; });
     * @endcode
     */
    template <typename F>
    NativeModuleExport& function(const char* name, F&& fn) {
        if (dry_run_) {
            export_names_.push_back(name);
        } else {
            JSValue jsFn = wrapFunction(ctx_, name, std::forward<F>(fn));
            JS_SetModuleExport(ctx_, m_, name, jsFn);
        }
        return *this;
    }

    // -----------------------------------------------------------------------
    // Value export
    // -----------------------------------------------------------------------

    /**
     * @brief Export a constant value from this module.
     *
     * @code
     * mod.value("PI", 3.14159265358979);
     * mod.value("VERSION", std::string("1.0.0"));
     * @endcode
     */
    template <typename V>
    NativeModuleExport& value(const char* name, V&& val) {
        if (dry_run_) {
            export_names_.push_back(name);
        } else {
            JSValue jsVal = JsConverter<std::decay_t<V>>::toJs(ctx_, std::forward<V>(val));
            JS_SetModuleExport(ctx_, m_, name, jsVal);
        }
        return *this;
    }

    // -----------------------------------------------------------------------
    // Enum export
    // -----------------------------------------------------------------------

    /**
     * @brief Export a single enum constant as a named export.
     *
     * @code
     * mod.enum_value("RED", Color::Red);
     * @endcode
     *
     * @tparam EnumT Enum type.
     * @param name Export name.
     * @param value The enum value.
     */
    template <typename EnumT>
    NativeModuleExport& enum_value(const char* name, EnumT value) {
        if (dry_run_) {
            export_names_.push_back(name);
        } else {
            JSValue jsVal = JsConverter<EnumT>::toJs(ctx_, value);
            JS_SetModuleExport(ctx_, m_, name, jsVal);
        }
        return *this;
    }

    /**
     * @brief Export an enum type as an object containing all given entries.
     *
     * The exported object is frozen (immutable). Each entry becomes a
     * property whose value is the underlying integer.
     *
     * @code
     * mod.enum_<Color>("Color", {
     *     {"RED",   Color::Red},
     *     {"GREEN", Color::Green},
     *     {"BLUE",  Color::Blue},
     * });
     * // JS: import { Color } from 'mymod';
     * //     Color.RED === 0
     * @endcode
     *
     * @tparam EnumT Enum type.
     * @param name Export name for the enum object.
     * @param entries Pairs of (name, value).
     */
    template <typename EnumT>
    NativeModuleExport& enum_(const char* name,
                              std::initializer_list<std::pair<const char*, EnumT>> entries) {
        if (dry_run_) {
            export_names_.push_back(name);
        } else {
            JSValue obj = JS_NewObject(ctx_);
            for (auto& [k, v] : entries) {
                JS_DefinePropertyValueStr(
                    ctx_, obj, k, JsConverter<EnumT>::toJs(ctx_, v),
                    JS_PROP_ENUMERABLE);
            }
            // Freeze the enum object to prevent mutation.
            JSValue global = JS_GetGlobalObject(ctx_);
            JSValue objectCtor = JS_GetPropertyStr(ctx_, global, "Object");
            JSValue freezeFn = JS_GetPropertyStr(ctx_, objectCtor, "freeze");
            JSValue argv[] = {obj};
            JSValue frozen = JS_Call(ctx_, freezeFn, objectCtor, 1, argv);
            JS_FreeValue(ctx_, frozen);
            JS_FreeValue(ctx_, freezeFn);
            JS_FreeValue(ctx_, objectCtor);
            JS_FreeValue(ctx_, global);
            JS_SetModuleExport(ctx_, m_, name, obj);
        }
        return *this;
    }

    // -----------------------------------------------------------------------
    // Class export
    // -----------------------------------------------------------------------

    /**
     * @brief Export a C++ class as a named export of this module.
     *
     * Accepts a configuration lambda that sets up the ClassBinder (constructor,
     * methods, properties, etc.). The binder is automatically finalized and
     * exported — no need to call `installAsModuleExport()` manually.
     *
     * In dry-run mode, only the export name is recorded; the lambda is NOT
     * called, so ClassBinder is never constructed with a null context.
     *
     * @code
     * mod.class_<Vec3>("Vec3", [](auto& cls) {
     *     cls.constructor<void(), void(double, double, double)>()
     *        .method("length", &Vec3::length)
     *        .method("dot", &Vec3::dot)
     *        .property("x",
     *            [](const Vec3& v) { return v.getX(); },
     *            [](Vec3& v, double val) { v.setX(val); });
     * });
     * @endcode
     *
     * @tparam T The C++ class to bind.
     * @tparam Base Optional base class.
     * @tparam Config Callable taking ClassBinder<T, Base>&.
     * @param name Export name (must match a declared export).
     * @param config Lambda to configure the ClassBinder.
     * @return Reference to this builder for chaining.
     */
    template <typename T, typename Base = void, typename Config>
    NativeModuleExport& class_(const char* name, Config&& config) {
        if (dry_run_) {
            export_names_.push_back(name);
        } else {
            ClassBinder<T, Base> binder(ctx_, name);
            config(binder);
            binder.installAsModuleExport(*this);
        }
        return *this;
    }

    /**
     * @brief Export a raw JSValue under the given name.
     *
     * This is useful for exporting class constructors or other pre-built values.
     *
     * @param name Export name.
     * @param val The JSValue to export (ownership is transferred to the module).
     * @return Reference to this builder for chaining.
     */
    NativeModuleExport& exportValue(const char* name, JSValue val) {
        if (dry_run_) {
            export_names_.push_back(name);
        } else {
            JS_SetModuleExport(ctx_, m_, name, val);
        }
        return *this;
    }

    // -----------------------------------------------------------------------
    // Accessors
    // -----------------------------------------------------------------------

    /// @brief Get the JSContext* (nullptr in dry-run mode).
    [[nodiscard]] JSContext* context() const noexcept { return ctx_; }

    /// @brief Get the JSModuleDef* (nullptr in dry-run mode).
    [[nodiscard]] JSModuleDef* moduleDef() const noexcept { return m_; }

    /// @brief Get collected export names (only meaningful after dry-run).
    [[nodiscard]] const std::vector<std::string>& exportNames() const noexcept {
        return export_names_;
    }

    // -----------------------------------------------------------------------
    // Finalize
    // -----------------------------------------------------------------------

    /**
     * @brief Finalize the module export. Returns 0 on success.
     *
     * Call this as the return value of your init callback (real mode only).
     */
    [[nodiscard]] int finalize() const noexcept { return 0; }

private:
    JSContext* ctx_;
    JSModuleDef* m_;
    bool dry_run_;
    std::vector<std::string> export_names_;
};

// ============================================================================
// Helper: create a native module with declared exports (manual API)
// ============================================================================

/**
 * @brief Create a native ES module with declared exports.
 *
 * This helper wraps `JS_NewCModule` + `JS_AddModuleExport` for each export name.
 * Use it in your `js_init_module` implementation when you prefer manual control.
 *
 * @param ctx The JS context.
 * @param module_name The module name (passed by QuickJS).
 * @param init_func The module init callback (called when the module is instantiated).
 * @param export_names List of export names to declare.
 * @return The JSModuleDef*, or nullptr on failure.
 */
inline JSModuleDef* nativeModuleCreate(
    JSContext* ctx,
    const char* module_name,
    JSModuleInitFunc* init_func,
    std::initializer_list<const char*> export_names)
{
    JSModuleDef* m = JS_NewCModule(ctx, module_name, init_func);
    if (!m) return nullptr;

    for (const char* name : export_names) {
        JS_AddModuleExport(ctx, m, name);
    }
    return m;
}

// ============================================================================
// QJSBIND_NATIVE_MODULE — auto-generate js_init_module
// ============================================================================

/**
 * @brief Macro to auto-generate the `js_init_module` entry point.
 *
 * The user provides a setup function `void setup(NativeModuleExport& mod)`.
 * The macro:
 * 1. Runs `setup` in dry-run mode to collect all export names.
 * 2. Creates the module with `JS_NewCModule` and declares all exports.
 * 3. The init callback runs `setup` again in real mode to populate exports.
 *
 * @code
 * static void setupModule(NativeModuleExport& mod) {
 *     mod.function("add", [](double a, double b) { return a + b; });
 *     mod.value("PI", 3.14159265358979);
 * }
 * QJSBIND_NATIVE_MODULE(setupModule)
 * @endcode
 */
#define QJSBIND_NATIVE_MODULE(setup_func)                                      \
    static int qjsbind_module_init_(JSContext* ctx, JSModuleDef* m) {           \
        qjsbind::NativeModuleExport mod(ctx, m);                               \
        setup_func(mod);                                                        \
        return mod.finalize();                                                  \
    }                                                                           \
    extern "C" JS_MODULE_EXTERN JSModuleDef*                                    \
    js_init_module(JSContext* ctx, const char* module_name) {                    \
        /* Dry-run: collect export names */                                     \
        qjsbind::NativeModuleExport dryRun(nullptr, nullptr);                  \
        setup_func(dryRun);                                                     \
        /* Create the module and declare exports */                             \
        JSModuleDef* m = JS_NewCModule(ctx, module_name, qjsbind_module_init_); \
        if (!m) return nullptr;                                                 \
        for (const auto& name : dryRun.exportNames()) {                         \
            JS_AddModuleExport(ctx, m, name.c_str());                           \
        }                                                                       \
        return m;                                                               \
    }

// ============================================================================
// ClassBinder::installAsModuleExport — deferred definition
// ============================================================================
// Defined here (not in class_binder.hpp) because it needs the complete type
// of NativeModuleExport, which is declared after class_binder.hpp.

template <typename T, typename Base>
void ClassBinder<T, Base>::installAsModuleExport(NativeModuleExport& mod) {
    JSValue ctorFunc = buildConstructor();
    mod.exportValue(name_.c_str(), ctorFunc);
}

} // namespace qjsbind
