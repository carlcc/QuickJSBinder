/**
 * @file js_proxy.hpp
 * @brief Lazy property proxy for intuitive JS object manipulation from C++.
 *
 * JsProxy enables a sol2-style API for accessing and modifying JavaScript
 * objects from C++. It supports:
 *
 * - **Chained property access**: `ctx["path"]["to"]["object"]`
 * - **Assignment**: `ctx["math"]["add"] = someFunction;`
 * - **Function calls**: `ctx["math"]["add"](1, 2)`
 * - **Value retrieval**: `ctx["path"]["to"]["value"].get<int>()`
 * - **JsValue retrieval**: `ctx["path"].get()` returns a JsValue
 *
 * All property chains are lazily evaluated — intermediate proxies only
 * record the path, and the actual JS property traversal happens when
 * the proxy is consumed (assigned to, called, or get() is invoked).
 *
 * @note Part of the QuickJSBinder header-only C++17 library.
 */
#pragma once

#include <cassert>
#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>

#include "js_value.hpp"
#include "js_context.hpp"
#include "js_converter.hpp"
#include "function_wrapper.hpp"
#include "js_module.hpp"

namespace qjsbind {

/**
 * @brief A lazy property proxy for chained JS object access from C++.
 *
 * JsProxy holds a reference to a parent JS object and a property key. It
 * does NOT immediately resolve the property — instead it records the access
 * chain and resolves lazily when consumed. This enables the fluent syntax:
 *
 * @code
 * ctx["math"]["add"] = [](double a, double b) { return a + b; };
 * double result = ctx["math"]["add"](1.0, 2.0).get<double>();
 * @endcode
 *
 * Proxies are lightweight (no heap allocation) and should be used as
 * temporaries. Do NOT store a JsProxy across JS operations that may
 * invalidate the underlying object.
 */
class JsProxy {
public:
    /**
     * @brief Construct a root proxy from an owned JsValue.
     *
     * The proxy takes shared ownership of the root object via JsValue's
     * copy semantics.
     *
     * @param ctx JS context.
     * @param root The root JS object (e.g., global object).
     */
    JsProxy(JSContext* ctx, JsValue root) noexcept
        : ctx_(ctx), root_(std::move(root)), key_(nullptr) {}

    /**
     * @brief Construct a child proxy accessing a property of the parent.
     *
     * @param parent The parent proxy.
     * @param key Property name (must remain valid for the proxy's lifetime).
     */
    JsProxy(const JsProxy& parent, const char* key) noexcept
        : ctx_(parent.ctx_), root_(parent.resolve()), key_(key) {}

    /// Default copy/move — JsValue handles refcounting.
    JsProxy(const JsProxy&) = default;
    JsProxy(JsProxy&&) = default;
    JsProxy& operator=(const JsProxy&) = delete; // Use set() instead
    JsProxy& operator=(JsProxy&&) = delete;

    // -----------------------------------------------------------------------
    // Chained property access
    // -----------------------------------------------------------------------

    /**
     * @brief Access a sub-property, returning a deeper proxy.
     *
     * @code
     * auto proxy = ctx["path"]["to"]["property"];
     * @endcode
     *
     * @param key Property name.
     * @return A new JsProxy pointing to the sub-property.
     */
    [[nodiscard]] JsProxy operator[](const char* key) const {
        return JsProxy(*this, key);
    }

    /**
     * @brief Access a sub-property via std::string.
     *
     * @param key Property name.
     * @return A new JsProxy pointing to the sub-property.
     */
    [[nodiscard]] JsProxy operator[](const std::string& key) const {
        // Store the key in a persistent location so the const char* remains valid.
        // We resolve immediately and use the resolved value as a new root.
        JsValue resolved = this->resolve();
        JsValue child = JsValue::adopt(ctx_, JS_GetPropertyStr(ctx_, resolved.value(), key.c_str()));
        return JsProxy(ctx_, std::move(child));
    }

    // -----------------------------------------------------------------------
    // Assignment (set property)
    // -----------------------------------------------------------------------

    /**
     * @brief Assign a C++ value to this property.
     *
     * The value is converted to JSValue via JsConverter<T>. For callable
     * types (function pointers, lambdas, std::function), the callable is
     * wrapped as a JS function automatically.
     *
     * @code
     * ctx["greeting"] = std::string("hello");
     * ctx["math"]["add"] = [](double a, double b) { return a + b; };
     * @endcode
     *
     * @tparam V The value type.
     * @param value The value to assign.
     */
    template <typename V>
    void set(V&& value) const {
        assert(key_ && "Cannot assign to a root proxy without a property key");
        JSValue jsVal = toJsValue(std::forward<V>(value));
        JS_SetPropertyStr(ctx_, root_.value(), key_, jsVal);
    }

    /**
     * @brief Assign a C++ value to this property using operator=.
     *
     * Syntactic sugar for set(). Returns void to prevent misuse.
     *
     * @code
     * ctx["x"] = 42;
     * ctx["fn"] = [](int a, int b) { return a + b; };
     * @endcode
     *
     * @tparam V The value type.
     * @param value The value to assign.
     */
    template <typename V>
    void operator=(V&& value) const {
        set(std::forward<V>(value));
    }

    // -----------------------------------------------------------------------
    // Value retrieval
    // -----------------------------------------------------------------------

    /**
     * @brief Resolve the proxy and retrieve the JS property as a JsValue.
     *
     * This performs the actual property lookup. If the proxy has no key
     * (i.e., it's a root proxy), returns the root object.
     *
     * @return JsValue wrapping the resolved property (owned).
     */
    [[nodiscard]] JsValue get() const {
        return resolve();
    }

    /**
     * @brief Resolve the proxy and convert the property to a C++ type.
     *
     * @code
     * int x = ctx["x"].get<int>();
     * std::string s = ctx["name"].get<std::string>();
     * @endcode
     *
     * @tparam T The target C++ type.
     * @return The converted value.
     */
    template <typename T>
    [[nodiscard]] T get() const {
        JsValue val = resolve();
        return JsConverter<std::decay_t<T>>::fromJs(ctx_, val.value());
    }

    // -----------------------------------------------------------------------
    // Function call
    // -----------------------------------------------------------------------

    /**
     * @brief Call the resolved property as a JS function.
     *
     * Arguments are automatically converted from C++ to JSValue via
     * JsConverter<T>. The return value is wrapped in a JsProxy so you
     * can chain `.get<T>()` on the result.
     *
     * @code
     * auto result = ctx["math"]["add"](1, 2);
     * int sum = result.get<int>();
     * // Or directly:
     * int sum = ctx["math"]["add"](1, 2).get<int>();
     * @endcode
     *
     * @tparam Args Argument types.
     * @param args The arguments to pass.
     * @return A JsProxy wrapping the function's return value.
     */
    template <typename... Args>
    [[nodiscard]] JsProxy operator()(Args&&... args) const {
        JsValue fn = resolve();
        constexpr size_t N = sizeof...(Args);
        JSValue argv[N > 0 ? N : 1];

        if constexpr (N > 0) {
            size_t i = 0;
            ((argv[i++] = JsConverter<std::decay_t<Args>>::toJs(ctx_, std::forward<Args>(args))), ...);
        }

        JSValue result = JS_Call(ctx_, fn.value(), JS_UNDEFINED,
                                 static_cast<int>(N), argv);

        // Free argument JSValues.
        for (size_t i = 0; i < N; ++i) {
            JS_FreeValue(ctx_, argv[i]);
        }

        return JsProxy(ctx_, JsValue::adopt(ctx_, result));
    }

    // -----------------------------------------------------------------------
    // Truthiness / existence checks
    // -----------------------------------------------------------------------

    /**
     * @brief Check if the resolved value is undefined.
     * @return True if the property is undefined.
     */
    [[nodiscard]] bool isUndefined() const {
        JsValue val = resolve();
        return val.isUndefined();
    }

    /**
     * @brief Check if the resolved value is null.
     * @return True if the property is null.
     */
    [[nodiscard]] bool isNull() const {
        JsValue val = resolve();
        return val.isNull();
    }

    /**
     * @brief Check if the resolved value is a function.
     * @return True if the property is callable.
     */
    [[nodiscard]] bool isFunction() const {
        JsValue val = resolve();
        return val.isFunction();
    }

    /**
     * @brief Check if the resolved value is an object.
     * @return True if the property is an object.
     */
    [[nodiscard]] bool isObject() const {
        JsValue val = resolve();
        return val.isObject();
    }

    /**
     * @brief Get the associated JS context.
     * @return The JSContext pointer.
     */
    [[nodiscard]] JSContext* context() const noexcept { return ctx_; }

private:
    JSContext* ctx_;
    JsValue root_;        ///< The parent object (or resolved root).
    const char* key_;     ///< Property key (nullptr for root/resolved proxies).

    /**
     * @brief Resolve the proxy to a JsValue by performing the property lookup.
     *
     * If key_ is nullptr, returns a copy of root_ (already resolved).
     * Otherwise, looks up root_[key_].
     *
     * @return JsValue wrapping the resolved property (owned).
     */
    [[nodiscard]] JsValue resolve() const {
        if (!key_) {
            return root_; // Copy (JsValue handles dup).
        }
        return JsValue::adopt(ctx_, JS_GetPropertyStr(ctx_, root_.value(), key_));
    }

    // -----------------------------------------------------------------------
    // toJsValue helper: convert any C++ value to JSValue
    // -----------------------------------------------------------------------

    /**
     * @brief Convert a C++ value to JSValue, with special handling for callables.
     *
     * For callable types (lambdas, function pointers, std::function), this
     * wraps them as JS functions. For other types, uses JsConverter<T>.
     *
     * @tparam V The value type.
     * @param value The C++ value.
     * @return Owned JSValue.
     */
    template <typename V>
    JSValue toJsValue(V&& value) const {
        using DecayV = std::decay_t<V>;
        if constexpr (std::is_same_v<DecayV, JSValue>) {
            // Raw JSValue: pass through (caller transfers ownership).
            return value;
        } else if constexpr (std::is_same_v<DecayV, JsValue>) {
            // JsValue: dup it.
            return JS_DupValue(ctx_, value.value());
        } else if constexpr (isCallable<DecayV>()) {
            // Callable: wrap as JS function.
            return wrapFunction(ctx_, key_ ? key_ : "<anonymous>", std::forward<V>(value));
        } else {
            // Other types: use JsConverter.
            return JsConverter<DecayV>::toJs(ctx_, value);
        }
    }

    /**
     * @brief Detect if a type is a callable (has operator() or is a function pointer).
     *
     * Returns true for lambdas, functors, std::function, and function pointers.
     * Returns false for arithmetic types, strings, etc.
     */
    template <typename V>
    static constexpr bool isCallable() {
        if constexpr (std::is_function_v<std::remove_pointer_t<V>>) {
            return true;  // Function pointer.
        } else if constexpr (std::is_arithmetic_v<V> || std::is_enum_v<V>) {
            return false;
        } else if constexpr (std::is_same_v<V, std::string> || std::is_same_v<V, const char*>) {
            return false;
        } else if constexpr (std::is_same_v<V, JsValue> || std::is_same_v<V, JSValue>) {
            return false;
        } else {
            return hasCallOperator<V>(0);
        }
    }

    /// SFINAE helper to detect operator() (works for any arity).
    template <typename V>
    static constexpr auto hasCallOperator(int) -> decltype(&V::operator(), true) {
        return true;
    }
    template <typename V>
    static constexpr bool hasCallOperator(...) {
        return false;
    }
};

// ============================================================================
// JsContext extensions — operator[] for proxy access
// ============================================================================

// We add operator[] to JsContext via a free function pattern.
// Since JsContext is defined in js_context.hpp, we extend it here.

/**
 * @brief Create a proxy for accessing global properties of a JsContext.
 *
 * This enables the sol2-like syntax:
 * @code
 * auto proxy = jsContextProxy(ctx, "math");
 * proxy["add"] = [](double a, double b) { return a + b; };
 * @endcode
 *
 * @param ctx The JsContext.
 * @param key The global property name.
 * @return A JsProxy for the global property.
 */
inline JsProxy jsContextProxy(JsContext& ctx, const char* key) {
    JsValue global = ctx.globalObject();
    JsProxy root(ctx.get(), std::move(global));
    return root[key];
}

} // namespace qjsbind

// ============================================================================
// Deferred inline definition: JsContext::operator[]
// ============================================================================

inline qjsbind::JsProxy qjsbind::JsContext::operator[](const char* key) {
    JsValue global = globalObject();
    JsProxy root(ctx_, std::move(global));
    return root[key];
}

// ============================================================================
// Deferred inline definition: JsContext::module()
// ============================================================================

inline qjsbind::JsProxy qjsbind::JsContext::module(const char* name) {
    JsValue mod = getModule(name);
    return JsProxy(ctx_, std::move(mod));
}

// ============================================================================
// Deferred inline definition: JsValue::operator[]
// ============================================================================

inline qjsbind::JsProxy qjsbind::JsValue::operator[](const char* key) const {
    // Create a root proxy from this JsValue (dup), then descend into key.
    JsProxy root(ctx_, JsValue::dup(ctx_, val_));
    return root[key];
}

// ============================================================================
// Deferred inline definition: JsModule::operator[]
// ============================================================================

inline qjsbind::JsProxy qjsbind::JsModule::operator[](const char* key) const {
    // Create a proxy from the module's JS object, then descend into key.
    JsProxy root(ctx_, JsValue::dup(ctx_, obj_.value()));
    return root[key];
}
