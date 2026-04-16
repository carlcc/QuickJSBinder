/**
 * @file js_value.hpp
 * @brief RAII wrapper for QuickJS JSValue.
 *
 * JsValue automatically manages JSValue reference counting:
 * - Destructor calls JS_FreeValue.
 * - Copy constructor/assignment calls JS_DupValue.
 * - Move constructor/assignment transfers ownership (source becomes JS_UNDEFINED).
 * - adopt() accepts an already-owned JSValue without DupValue.
 * - release() surrenders ownership and returns the raw JSValue.
 *
 * @note Part of the QuickJSBinder header-only C++17 library.
 */
#pragma once

#include <cassert>
#include <cstddef>
#include <string>
#include <utility>

extern "C" {
#include "quickjs.h"
}

namespace qjsbind {

// Forward declaration for JsProxy (defined in js_proxy.hpp).
class JsProxy;

// ============================================================================
// JsValue — RAII wrapper for JSValue
// ============================================================================

/**
 * @brief RAII wrapper for QuickJS JSValue with automatic reference counting.
 *
 * A JsValue always holds a valid JSContext* and a JSValue. On destruction it
 * frees the value. Copy increments the refcount, move transfers ownership.
 *
 * @note JsValue ONLY accepts owned JSValues. For borrowed values (e.g. from
 * JSValueConst parameters), duplicate first with JS_DupValue before wrapping.
 */
class JsValue {
public:
    /**
     * @brief Construct an empty JsValue holding JS_UNDEFINED.
     * @param ctx The JS context (must remain valid for JsValue's lifetime).
     */
    explicit JsValue(JSContext* ctx) noexcept
        : ctx_(ctx), val_(JS_UNDEFINED) {}

    /**
     * @brief Adopt an already-owned JSValue (no DupValue).
     *
     * Use this factory when the JSValue was returned from a QuickJS API that
     * transfers ownership (e.g. JS_Eval, JS_Call, JS_GetPropertyStr).
     *
     * @param ctx JS context.
     * @param val Owned JSValue.
     * @return JsValue managing the value.
     */
    [[nodiscard]] static JsValue adopt(JSContext* ctx, JSValue val) noexcept {
        JsValue jv(ctx);
        jv.val_ = val;
        return jv;
    }

    /**
     * @brief Wrap a borrowed JSValue by duplicating it.
     *
     * Calls JS_DupValue so the JsValue has its own reference.
     *
     * @param ctx JS context.
     * @param val Borrowed JSValue (JSValueConst).
     * @return JsValue managing a new reference.
     */
    [[nodiscard]] static JsValue dup(JSContext* ctx, JSValueConst val) noexcept {
        JsValue jv(ctx);
        jv.val_ = JS_DupValue(ctx, val);
        return jv;
    }

    /// Destructor: free the owned JSValue.
    ~JsValue() {
        JS_FreeValue(ctx_, val_);
    }

    /// Copy constructor: increments refcount via JS_DupValue.
    JsValue(const JsValue& other) noexcept
        : ctx_(other.ctx_), val_(JS_DupValue(other.ctx_, other.val_)) {}

    /// Copy assignment: free old, dup new.
    JsValue& operator=(const JsValue& other) noexcept {
        if (this != &other) {
            JS_FreeValue(ctx_, val_);
            ctx_ = other.ctx_;
            val_ = JS_DupValue(other.ctx_, other.val_);
        }
        return *this;
    }

    /// Move constructor: transfer ownership, source becomes JS_UNDEFINED.
    JsValue(JsValue&& other) noexcept
        : ctx_(other.ctx_), val_(other.val_) {
        other.val_ = JS_UNDEFINED;
    }

    /// Move assignment: free old, transfer ownership.
    JsValue& operator=(JsValue&& other) noexcept {
        if (this != &other) {
            JS_FreeValue(ctx_, val_);
            ctx_ = other.ctx_;
            val_ = other.val_;
            other.val_ = JS_UNDEFINED;
        }
        return *this;
    }

    // -----------------------------------------------------------------------
    // Ownership management
    // -----------------------------------------------------------------------

    /**
     * @brief Release ownership and return the raw JSValue.
     *
     * After this call the JsValue holds JS_UNDEFINED and will NOT free
     * the original value. The caller takes ownership.
     *
     * @return The previously managed JSValue.
     */
    [[nodiscard]] JSValue release() noexcept {
        JSValue v = val_;
        val_ = JS_UNDEFINED;
        return v;
    }

    /**
     * @brief Reset the JsValue, freeing the old value and adopting a new one.
     * @param val New owned JSValue (default JS_UNDEFINED).
     */
    void reset(JSValue val = JS_UNDEFINED) noexcept {
        JS_FreeValue(ctx_, val_);
        val_ = val;
    }

    // -----------------------------------------------------------------------
    // Accessors
    // -----------------------------------------------------------------------

    /** @brief Get the raw JSValue (borrowed — do NOT free). */
    [[nodiscard]] JSValue value() const noexcept { return val_; }

    /** @brief Implicit conversion to JSValueConst for QuickJS API calls. */
    operator JSValueConst() const noexcept { return val_; }

    /** @brief Get the associated context. */
    [[nodiscard]] JSContext* context() const noexcept { return ctx_; }

    // -----------------------------------------------------------------------
    // Type checks
    // -----------------------------------------------------------------------

    /** @brief True if the value is undefined. */
    [[nodiscard]] bool isUndefined() const noexcept { return JS_IsUndefined(val_); }

    /** @brief True if the value is null. */
    [[nodiscard]] bool isNull() const noexcept { return JS_IsNull(val_); }

    /** @brief True if the value is a boolean. */
    [[nodiscard]] bool isBool() const noexcept { return JS_IsBool(val_); }

    /** @brief True if the value is a number (int or float). */
    [[nodiscard]] bool isNumber() const noexcept { return JS_IsNumber(val_); }

    /** @brief True if the value is a string. */
    [[nodiscard]] bool isString() const noexcept { return JS_IsString(val_); }

    /** @brief True if the value is an object. */
    [[nodiscard]] bool isObject() const noexcept { return JS_IsObject(val_); }

    /** @brief True if the value is a function. */
    [[nodiscard]] bool isFunction() const noexcept { return JS_IsFunction(ctx_, val_); }

    /** @brief True if the value is an array. */
    [[nodiscard]] bool isArray() const noexcept { return JS_IsArray(val_); }

    /** @brief True if the value is an exception marker. */
    [[nodiscard]] bool isException() const noexcept { return JS_IsException(val_); }

    /** @brief True if the value is an Error object. */
    [[nodiscard]] bool isError() const noexcept { return JS_IsError(val_); }

    // -----------------------------------------------------------------------
    // Type-safe value extraction
    // -----------------------------------------------------------------------

    /**
     * @brief Convert to bool.
     * @return The boolean value, or false on error.
     */
    [[nodiscard]] bool toBool() const {
        return JS_ToBool(ctx_, val_) > 0;
    }

    /**
     * @brief Convert to int32.
     * @param ok Optional output flag (true on success).
     * @return The int32 value, or 0 on error.
     */
    [[nodiscard]] int32_t toInt32(bool* ok = nullptr) const {
        int32_t result = 0;
        int ret = JS_ToInt32(ctx_, &result, val_);
        if (ok) *ok = (ret == 0);
        return result;
    }

    /**
     * @brief Convert to int64.
     * @param ok Optional output flag.
     * @return The int64 value, or 0 on error.
     */
    [[nodiscard]] int64_t toInt64(bool* ok = nullptr) const {
        int64_t result = 0;
        int ret = JS_ToInt64(ctx_, &result, val_);
        if (ok) *ok = (ret == 0);
        return result;
    }

    /**
     * @brief Convert to double.
     * @param ok Optional output flag.
     * @return The double value, or 0.0 on error.
     */
    [[nodiscard]] double toDouble(bool* ok = nullptr) const {
        double result = 0.0;
        int ret = JS_ToFloat64(ctx_, &result, val_);
        if (ok) *ok = (ret == 0);
        return result;
    }

    /**
     * @brief Convert to std::string.
     *
     * Returns an empty string if conversion fails.
     *
     * @return The string value.
     */
    [[nodiscard]] std::string toString() const {
        size_t len = 0;
        const char* cstr = JS_ToCStringLen(ctx_, &len, val_);
        if (!cstr) return {};
        std::string result(cstr, len);
        JS_FreeCString(ctx_, cstr);
        return result;
    }

    // -----------------------------------------------------------------------
    // Object property access (convenience)
    // -----------------------------------------------------------------------

    /**
     * @brief Get a property by name.
     * @param name Property name (ASCII/UTF-8).
     * @return JsValue wrapping the property value (owned).
     */
    [[nodiscard]] JsValue getProperty(const char* name) const {
        return JsValue::adopt(ctx_, JS_GetPropertyStr(ctx_, val_, name));
    }

    /**
     * @brief Set a property by name.
     * @param name Property name.
     * @param val Value to set (ownership transferred to QuickJS).
     * @return 0 on success, -1 on failure.
     */
    int setProperty(const char* name, JSValue val) const {
        return JS_SetPropertyStr(ctx_, val_, name, val);
    }

    /**
     * @brief Get a property by index.
     * @param idx Array index.
     * @return JsValue wrapping the element (owned).
     */
    [[nodiscard]] JsValue getPropertyUint32(uint32_t idx) const {
        return JsValue::adopt(ctx_, JS_GetPropertyUint32(ctx_, val_, idx));
    }

    // -----------------------------------------------------------------------
    // Proxy access (sol2-like syntax on JsValue)
    // -----------------------------------------------------------------------

    /**
     * @brief Access a property via proxy, enabling sol2-like syntax on any JsValue.
     *
     * This allows using JsValue as a proxy root for chained access:
     * @code
     * JsValue obj = ctx.globalObject();
     * obj["math"]["add"](1, 2).get<int>();
     *
     * JsValue moduleVal = engine.jsValue();
     * moduleVal["version"].get<std::string>();
     * moduleVal["newProp"] = 42;
     * @endcode
     *
     * @param key Property name.
     * @return A JsProxy for the property.
     *
     * @note Defined after JsProxy is fully declared (in js_proxy.hpp).
     */
    inline JsProxy operator[](const char* key) const;

private:
    JSContext* ctx_;
    JSValue val_;
};

} // namespace qjsbind
