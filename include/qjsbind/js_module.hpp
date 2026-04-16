/**
 * @file js_module.hpp
 * @brief Module binding facility for QuickJSBinder.
 *
 * JsModule provides a convenient API for grouping related bindings (classes,
 * functions, variables) into a namespace-like JS object. Modules can be nested
 * and can either create new objects or bind into existing ones.
 *
 * Usage:
 * @code
 * JsModule mod(ctx, "math");
 * mod.function("add", [](double a, double b) { return a + b; });
 * mod.function("sub", [](double a, double b) { return a - b; });
 * mod.value("PI", 3.14159265358979);
 * mod.install();  // Creates globalThis.math = { add, sub, PI }
 * @endcode
 *
 * Nested modules:
 * @code
 * JsModule root(ctx, "app");
 * auto& utils = root.submodule("utils");
 * utils.function("clamp", ...);
 * root.install();  // Creates globalThis.app.utils.clamp(...)
 * @endcode
 *
 * @note Part of the QuickJSBinder header-only C++17 library.
 */
#pragma once

#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "js_value.hpp"
#include "js_converter.hpp"
#include "function_wrapper.hpp"

namespace qjsbind {

// Forward declaration.
template <typename T, typename Base>
class ClassBinder;

/**
 * @brief A builder for grouping bindings into a JS object (module/namespace).
 *
 * JsModule creates a plain JS object and populates it with functions, values,
 * and sub-modules. Call install() to attach it to the global object or a
 * parent module.
 *
 * JsModule can also wrap an existing JS object (e.g., from a proxy or
 * a property chain) so that bindings are added to it.
 *
 * @note A JsModule does NOT correspond to an ES module — it's just a plain
 * object used as a namespace. For ES module support, use QuickJS's native
 * module system directly.
 */
class JsModule {
public:
    /**
     * @brief Create a new module that will be installed as a global property.
     *
     * @param ctx JS context.
     * @param name The module name (global property name).
     */
    JsModule(JSContext* ctx, const char* name)
        : ctx_(ctx)
        , name_(name)
        , obj_(JsValue::adopt(ctx, JS_NewObject(ctx)))
        , parent_obj_()
        , is_root_(true)
    {}

    /**
     * @brief Create a module that will be installed as a property of an existing object.
     *
     * Use this to attach a module to an existing JS object instead of the global.
     *
     * @param ctx JS context.
     * @param name Property name on the parent.
     * @param parent The parent JS object (borrowed reference — will be duped).
     */
    JsModule(JSContext* ctx, const char* name, JSValueConst parent)
        : ctx_(ctx)
        , name_(name)
        , obj_(JsValue::adopt(ctx, JS_NewObject(ctx)))
        , parent_obj_(JsValue::dup(ctx, parent))
        , is_root_(false)
    {}

    /// Non-copyable but movable.
    JsModule(const JsModule&) = delete;
    JsModule& operator=(const JsModule&) = delete;
    JsModule(JsModule&&) = default;
    JsModule& operator=(JsModule&&) = default;

    ~JsModule() = default;

    // -----------------------------------------------------------------------
    // Function registration
    // -----------------------------------------------------------------------

    /**
     * @brief Register a function in this module.
     *
     * The callable can be a function pointer, lambda, or std::function.
     * It is wrapped as a JS function and added as a property of the module object.
     *
     * @code
     * mod.function("add", [](double a, double b) { return a + b; });
     * mod.function("greet", &my_greet_function);
     * @endcode
     *
     * @tparam F Callable type.
     * @param name Function name.
     * @param fn The callable.
     * @return Reference to this JsModule for chaining.
     */
    template <typename F>
    JsModule& function(const char* name, F&& fn) {
        JSValue jsFn = wrapFunction(ctx_, name, std::forward<F>(fn));
        JS_DefinePropertyValueStr(ctx_, obj_.value(), name, jsFn,
                                  JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
        return *this;
    }

    /**
     * @brief Register multiple overloaded functions under the same name.
     *
     * @code
     * mod.function("process",
     *     [](int x) { return x * 2; },
     *     [](std::string s) { return s + "!"; });
     * @endcode
     *
     * @tparam Fns Callable types.
     * @param name Function name.
     * @param fns The callables.
     * @return Reference to this JsModule for chaining.
     */
    template <typename F1, typename F2, typename... Rest>
    JsModule& function(const char* name, F1&& fn1, F2&& fn2, Rest&&... rest) {
        auto* table = new OverloadTable();
        addFreeEntries(*table, std::forward<F1>(fn1), std::forward<F2>(fn2),
                       std::forward<Rest>(rest)...);
        JSValue jsFn = createOverloadedFunction(ctx_, name, table);
        JS_DefinePropertyValueStr(ctx_, obj_.value(), name, jsFn,
                                  JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
        return *this;
    }

    // -----------------------------------------------------------------------
    // Value registration
    // -----------------------------------------------------------------------

    /**
     * @brief Register a constant value in this module.
     *
     * The value is converted via JsConverter<T> and set as a non-writable
     * property (constant).
     *
     * @code
     * mod.value("PI", 3.14159265358979);
     * mod.value("VERSION", std::string("1.0.0"));
     * @endcode
     *
     * @tparam V Value type.
     * @param name Property name.
     * @param val The value.
     * @return Reference to this JsModule for chaining.
     */
    template <typename V>
    JsModule& value(const char* name, V&& val) {
        JSValue jsVal = JsConverter<std::decay_t<V>>::toJs(ctx_, std::forward<V>(val));
        JS_DefinePropertyValueStr(ctx_, obj_.value(), name, jsVal,
                                  JS_PROP_ENUMERABLE);
        return *this;
    }

    /**
     * @brief Register a mutable variable in this module.
     *
     * The value is converted via JsConverter<T> and set as a writable property.
     *
     * @code
     * mod.variable("counter", 0);
     * @endcode
     *
     * @tparam V Value type.
     * @param name Property name.
     * @param val The initial value.
     * @return Reference to this JsModule for chaining.
     */
    template <typename V>
    JsModule& variable(const char* name, V&& val) {
        JSValue jsVal = JsConverter<std::decay_t<V>>::toJs(ctx_, std::forward<V>(val));
        JS_DefinePropertyValueStr(ctx_, obj_.value(), name, jsVal,
                                  JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
        return *this;
    }

    // -----------------------------------------------------------------------
    // Sub-module registration
    // -----------------------------------------------------------------------

    /**
     * @brief Create and return a nested sub-module.
     *
     * The sub-module is a child JS object that will be installed as a property
     * of this module when install() is called.
     *
     * @code
     * auto& sub = mod.submodule("utils");
     * sub.function("clamp", ...);
     * mod.install(); // Installs both mod and mod.utils
     * @endcode
     *
     * @param name Sub-module name.
     * @return Reference to the sub-module for chaining.
     */
    JsModule& submodule(const char* name) {
        submodules_.emplace_back(
            std::make_unique<JsModule>(ctx_, name, obj_.value()));
        return *submodules_.back();
    }

    // -----------------------------------------------------------------------
    // Raw JS object access
    // -----------------------------------------------------------------------

    /**
     * @brief Get the underlying JS object of this module (borrowed reference).
     *
     * Useful for passing to ClassBinder::installInto() or other APIs
     * that need the raw JSValueConst.
     *
     * @return Borrowed JSValueConst of the module object.
     */
    [[nodiscard]] JSValueConst object() const noexcept {
        return obj_.value();
    }

    /**
     * @brief Get the module's JS object as an owned JsValue.
     *
     * This is particularly important for game engines and other dynamic
     * scripting scenarios where you need to:
     * - Store the module object for later use (e.g., passing to a script system)
     * - Pass the module to other C++ systems as a JsValue
     * - Dynamically add/remove properties from C++ after installation
     * - Reference the module from multiple subsystems
     *
     * The returned JsValue is a new reference (JS_DupValue). It remains valid
     * even after the JsModule object goes out of scope, as long as the
     * JsContext is alive.
     *
     * @code
     * JsModule engine(ctx, "Engine");
     * engine.function("log", &my_log);
     * engine.install();
     *
     * // Store the module value for later dynamic manipulation.
     * JsValue engineObj = engine.jsValue();
     *
     * // Later, dynamically add a new property:
     * JS_SetPropertyStr(ctx, engineObj.value(), "fps",
     *                   JS_NewFloat64(ctx, 60.0));
     * @endcode
     *
     * @return JsValue wrapping the module object (owned, ref-counted).
     */
    [[nodiscard]] JsValue jsValue() const {
        return JsValue::dup(ctx_, obj_.value());
    }

    /**
     * @brief Get the JS context.
     * @return The JSContext pointer.
     */
    [[nodiscard]] JSContext* context() const noexcept { return ctx_; }

    /**
     * @brief Get the module name.
     * @return The module name string.
     */
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    // -----------------------------------------------------------------------
    // Proxy access (sol2-like syntax on JsModule)
    // -----------------------------------------------------------------------

    /**
     * @brief Access a module property via proxy, enabling sol2-like syntax.
     *
     * This allows chained access, assignment, and function calls directly
     * on a JsModule, which is especially useful for dynamic scripting:
     *
     * @code
     * JsModule engine(ctx, "Engine");
     * engine.function("log", &my_log);
     * engine.install();
     *
     * // Use module as proxy root:
     * engine["fps"] = 60.0;
     * engine["log"]("hello via proxy");
     * std::string ver = engine["version"].get<std::string>();
     * @endcode
     *
     * @param key Property name.
     * @return A JsProxy for the property.
     *
     * @note Defined after JsProxy is fully declared (in js_proxy.hpp).
     */
    inline JsProxy operator[](const char* key) const;

    // -----------------------------------------------------------------------
    // Installation
    // -----------------------------------------------------------------------

    /**
     * @brief Install this module (and all sub-modules) into its parent.
     *
     * For root modules, the parent is the global object.
     * For sub-modules, the parent is the enclosing module object.
     *
     * This method should be called once after all registrations are complete.
     * It recursively installs all sub-modules.
     */
    void install() {
        // First, install all sub-modules into this module's object.
        for (auto& sub : submodules_) {
            sub->installInto(obj_.value());
        }
        submodules_.clear();

        if (is_root_) {
            // Install into global.
            JSValue global = JS_GetGlobalObject(ctx_);
            JS_SetPropertyStr(ctx_, global, name_.c_str(),
                              JS_DupValue(ctx_, obj_.value()));
            JS_FreeValue(ctx_, global);
        } else if (parent_obj_) {
            // Install into parent object.
            JS_SetPropertyStr(ctx_, parent_obj_->value(), name_.c_str(),
                              JS_DupValue(ctx_, obj_.value()));
        }
    }

private:
    JSContext* ctx_;
    std::string name_;
    JsValue obj_;                                     ///< The module's JS object.
    std::optional<JsValue> parent_obj_;               ///< Parent object (for sub-modules).
    bool is_root_;                                    ///< True if installed into global.
    std::vector<std::unique_ptr<JsModule>> submodules_; ///< Nested sub-modules.

    /**
     * @brief Install this module into a specific parent JS object.
     * @param parent The parent JS object.
     */
    void installInto(JSValueConst parent) {
        // Install sub-modules first.
        for (auto& sub : submodules_) {
            sub->installInto(obj_.value());
        }
        submodules_.clear();

        JS_SetPropertyStr(ctx_, parent, name_.c_str(),
                          JS_DupValue(ctx_, obj_.value()));
    }

    /// Helper: add free function entries to an overload table.
    template <typename F>
    static void addFreeEntries(OverloadTable& table, F&& fn) {
        table.entries.push_back(detail::makeFreeFunctionEntry(std::forward<F>(fn)));
    }

    template <typename F, typename... Rest>
    static void addFreeEntries(OverloadTable& table, F&& fn, Rest&&... rest) {
        table.entries.push_back(detail::makeFreeFunctionEntry(std::forward<F>(fn)));
        addFreeEntries(table, std::forward<Rest>(rest)...);
    }
};

} // namespace qjsbind
