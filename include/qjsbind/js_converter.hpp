/**
 * @file js_converter.hpp
 * @brief Type conversion traits for bidirectional C++ <-> JSValue conversion.
 *
 * JsConverter<T> provides static methods `toJs()` and `fromJs()` for each
 * supported type. Built-in specializations cover:
 * - Arithmetic types: int, unsigned, int64_t, uint64_t, double, float
 * - bool
 * - std::string, const char*
 * - std::vector<T>
 * - std::optional<T>
 * - std::function<R(Args...)> (JS function → C++ callable)
 * - JsValue (passthrough)
 * - JSValue (raw passthrough)
 *
 * Users may add specializations for custom types (ClassBinder does this
 * automatically for registered classes).
 *
 * @note Part of the QuickJSBinder header-only C++17 library.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "js_value.hpp"

namespace qjsbind {

// ============================================================================
// Primary template (unspecialized — produces a compile error)
// ============================================================================

/**
 * @brief Primary type conversion trait.
 *
 * Specialize this template for each type you want to convert to/from JSValue.
 * The primary template is intentionally left incomplete to produce clear
 * compile errors for unsupported types.
 *
 * Required static methods in each specialization:
 * @code
 * static JSValue toJs(JSContext* ctx, const T& value);
 * static T fromJs(JSContext* ctx, JSValueConst value);
 * @endcode
 *
 * @tparam T The C++ type to convert.
 * @tparam Enable SFINAE helper (default void).
 */
template <typename T, typename Enable = void>
struct JsConverter {
    // Intentionally incomplete — specialization required.
    // static JSValue toJs(JSContext* ctx, const T& value);
    // static T fromJs(JSContext* ctx, JSValueConst value);
};

// ============================================================================
// bool
// ============================================================================

/** @brief Specialization for bool. */
template <>
struct JsConverter<bool> {
    /** @brief Convert C++ bool to JS boolean. */
    static JSValue toJs(JSContext* ctx, bool value) {
        return JS_NewBool(ctx, value);
    }
    /** @brief Convert JS value to C++ bool. */
    static bool fromJs(JSContext* ctx, JSValueConst value) {
        return JS_ToBool(ctx, value) > 0;
    }
};

// ============================================================================
// Integer types
// ============================================================================

/** @brief Specialization for int32_t. */
template <>
struct JsConverter<int32_t> {
    static JSValue toJs(JSContext* ctx, int32_t value) {
        return JS_NewInt32(ctx, value);
    }
    static int32_t fromJs(JSContext* ctx, JSValueConst value) {
        int32_t result = 0;
        JS_ToInt32(ctx, &result, value);
        return result;
    }
};

/** @brief Specialization for uint32_t. */
template <>
struct JsConverter<uint32_t> {
    static JSValue toJs(JSContext* ctx, uint32_t value) {
        return JS_NewUint32(ctx, value);
    }
    static uint32_t fromJs(JSContext* ctx, JSValueConst value) {
        uint32_t result = 0;
        JS_ToUint32(ctx, &result, value);
        return result;
    }
};

/** @brief Specialization for int64_t. */
template <>
struct JsConverter<int64_t> {
    static JSValue toJs(JSContext* ctx, int64_t value) {
        return JS_NewInt64(ctx, value);
    }
    static int64_t fromJs(JSContext* ctx, JSValueConst value) {
        int64_t result = 0;
        JS_ToInt64(ctx, &result, value);
        return result;
    }
};

/** @brief Specialization for uint64_t (via BigInt). */
template <>
struct JsConverter<uint64_t> {
    static JSValue toJs(JSContext* ctx, uint64_t value) {
        return JS_NewBigUint64(ctx, value);
    }
    static uint64_t fromJs(JSContext* ctx, JSValueConst value) {
        uint64_t result = 0;
        JS_ToBigUint64(ctx, &result, value);
        return result;
    }
};

// ============================================================================
// Floating point types
// ============================================================================

/** @brief Specialization for double. */
template <>
struct JsConverter<double> {
    static JSValue toJs(JSContext* ctx, double value) {
        return JS_NewFloat64(ctx, value);
    }
    static double fromJs(JSContext* ctx, JSValueConst value) {
        double result = 0.0;
        JS_ToFloat64(ctx, &result, value);
        return result;
    }
};

/** @brief Specialization for float (promoted to double). */
template <>
struct JsConverter<float> {
    static JSValue toJs(JSContext* ctx, float value) {
        return JS_NewFloat64(ctx, static_cast<double>(value));
    }
    static float fromJs(JSContext* ctx, JSValueConst value) {
        return static_cast<float>(JsConverter<double>::fromJs(ctx, value));
    }
};

// ============================================================================
// String types
// ============================================================================

/** @brief Specialization for std::string. */
template <>
struct JsConverter<std::string> {
    static JSValue toJs(JSContext* ctx, const std::string& value) {
        return JS_NewStringLen(ctx, value.data(), value.size());
    }
    static std::string fromJs(JSContext* ctx, JSValueConst value) {
        size_t len = 0;
        const char* cstr = JS_ToCStringLen(ctx, &len, value);
        if (!cstr) return {};
        std::string result(cstr, len);
        JS_FreeCString(ctx, cstr);
        return result;
    }
};

/** @brief Specialization for const char* (toJs only; fromJs returns std::string). */
template <>
struct JsConverter<const char*> {
    static JSValue toJs(JSContext* ctx, const char* value) {
        return value ? JS_NewString(ctx, value) : JS_NULL;
    }
    // fromJs intentionally omitted — use JsConverter<std::string>::fromJs.
};

// ============================================================================
// std::vector<T>
// ============================================================================

/**
 * @brief Specialization for std::vector<T>.
 *
 * Maps to/from JS arrays. Elements are converted recursively via JsConverter<T>.
 */
template <typename T>
struct JsConverter<std::vector<T>> {
    static JSValue toJs(JSContext* ctx, const std::vector<T>& vec) {
        JSValue arr = JS_NewArray(ctx);
        if (JS_IsException(arr)) return arr;
        for (size_t i = 0; i < vec.size(); ++i) {
            JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i),
                                 JsConverter<T>::toJs(ctx, vec[i]));
        }
        return arr;
    }
    static std::vector<T> fromJs(JSContext* ctx, JSValueConst value) {
        std::vector<T> result;
        if (!JS_IsArray(value)) return result;
        int64_t len = 0;
        if (JS_GetLength(ctx, value, &len) < 0) return result;
        result.reserve(static_cast<size_t>(len));
        for (int64_t i = 0; i < len; ++i) {
            JSValue elem = JS_GetPropertyUint32(ctx, value, static_cast<uint32_t>(i));
            result.push_back(JsConverter<T>::fromJs(ctx, elem));
            JS_FreeValue(ctx, elem);
        }
        return result;
    }
};

// ============================================================================
// std::optional<T>
// ============================================================================

/**
 * @brief Specialization for std::optional<T>.
 *
 * Maps std::nullopt / empty to JS undefined; otherwise converts the contained value.
 */
template <typename T>
struct JsConverter<std::optional<T>> {
    static JSValue toJs(JSContext* ctx, const std::optional<T>& opt) {
        if (!opt.has_value()) return JS_UNDEFINED;
        return JsConverter<T>::toJs(ctx, *opt);
    }
    static std::optional<T> fromJs(JSContext* ctx, JSValueConst value) {
        if (JS_IsUndefined(value) || JS_IsNull(value)) return std::nullopt;
        return JsConverter<T>::fromJs(ctx, value);
    }
};

// ============================================================================
// std::function<R(Args...)> — JS function → C++ callable
// ============================================================================

/**
 * @brief Specialization for std::function<R(Args...)>.
 *
 * **toJs direction**: wraps a C++ std::function as a JS closure.
 * **fromJs direction**: wraps a JS function as a C++ std::function. The JS
 * function's reference is held via a shared_ptr<JsValue> so it is safe to
 * call after the original JSValue is freed.
 *
 * @tparam R Return type.
 * @tparam Args Argument types.
 */
template <typename R, typename... Args>
struct JsConverter<std::function<R(Args...)>> {
    static std::function<R(Args...)> fromJs(JSContext* ctx, JSValueConst value) {
        if (!JS_IsFunction(ctx, value)) {
            return nullptr;
        }
        // Hold a reference to the JS function via shared_ptr<JsValue>.
        auto jsFuncPtr = std::make_shared<JsValue>(JsValue::dup(ctx, value));
        return [ctx, jsFuncPtr](Args... args) -> R {
            // Convert C++ args to JSValue array.
            constexpr size_t N = sizeof...(Args);
            JSValue argv[N > 0 ? N : 1];
            if constexpr (N > 0) {
                size_t i = 0;
                ((argv[i++] = JsConverter<std::decay_t<Args>>::toJs(ctx, args)), ...);
            }
            JSValue result = JS_Call(ctx, jsFuncPtr->value(), JS_UNDEFINED,
                                     static_cast<int>(N), argv);
            // Free argument JSValues.
            for (size_t i = 0; i < N; ++i) {
                JS_FreeValue(ctx, argv[i]);
            }
            if constexpr (std::is_void_v<R>) {
                JS_FreeValue(ctx, result);
            } else {
                R ret = JsConverter<R>::fromJs(ctx, result);
                JS_FreeValue(ctx, result);
                return ret;
            }
        };
    }

    // toJs for std::function is more complex (requires closure creation).
    // This is handled in function_wrapper.hpp.
};

// ============================================================================
// JsValue passthrough
// ============================================================================

/** @brief Specialization for JsValue (passthrough with dup semantics). */
template <>
struct JsConverter<JsValue> {
    static JSValue toJs(JSContext* /*ctx*/, const JsValue& value) {
        // Dup because toJs returns an owned value.
        return JS_DupValue(value.context(), value.value());
    }
    static JsValue fromJs(JSContext* ctx, JSValueConst value) {
        return JsValue::dup(ctx, value);
    }
};

// ============================================================================
// JSValue raw passthrough
// ============================================================================

/** @brief Specialization for raw JSValue (zero-overhead passthrough). */
template <>
struct JsConverter<JSValue> {
    /** @brief Return a dup of the value (caller owns). */
    static JSValue toJs(JSContext* ctx, JSValue value) {
        return JS_DupValue(ctx, value);
    }
    /** @brief Return a dup of the value (caller owns). */
    static JSValue fromJs(JSContext* ctx, JSValueConst value) {
        return JS_DupValue(ctx, value);
    }
};

// ============================================================================
// SFINAE helpers for arithmetic types not explicitly specialized
// ============================================================================

/**
 * @brief Catch-all for arithmetic types not covered by explicit specializations.
 *
 * Promotes smaller integer types through int32_t and floating types through double.
 */
template <typename T>
struct JsConverter<T, std::enable_if_t<
    std::is_arithmetic_v<T> &&
    !std::is_same_v<T, bool> &&
    !std::is_same_v<T, int32_t> &&
    !std::is_same_v<T, uint32_t> &&
    !std::is_same_v<T, int64_t> &&
    !std::is_same_v<T, uint64_t> &&
    !std::is_same_v<T, double> &&
    !std::is_same_v<T, float>
>> {
    static JSValue toJs(JSContext* ctx, T value) {
        if constexpr (std::is_integral_v<T>) {
            if constexpr (sizeof(T) <= 4) {
                return JS_NewInt32(ctx, static_cast<int32_t>(value));
            } else {
                return JS_NewInt64(ctx, static_cast<int64_t>(value));
            }
        } else {
            return JS_NewFloat64(ctx, static_cast<double>(value));
        }
    }
    static T fromJs(JSContext* ctx, JSValueConst value) {
        if constexpr (std::is_integral_v<T>) {
            if constexpr (sizeof(T) <= 4) {
                return static_cast<T>(JsConverter<int32_t>::fromJs(ctx, value));
            } else {
                return static_cast<T>(JsConverter<int64_t>::fromJs(ctx, value));
            }
        } else {
            return static_cast<T>(JsConverter<double>::fromJs(ctx, value));
        }
    }
};

// ============================================================================
// std::reference_wrapper<T> — shared reference semantics
// ============================================================================

/**
 * @brief Specialization for std::reference_wrapper<T>.
 *
 * When a std::reference_wrapper<T> is converted to JS, the resulting JS object
 * shares the same native C++ object as the original reference. Both sides see
 * each other's modifications (shared/borrowed semantics).
 *
 * When converted from JS back to C++, it returns a std::reference_wrapper
 * pointing to the same native T* stored in the JS object.
 *
 * @note T must be a ClassBinder-registered type with QJSBIND_DECLARE_CONVERTER.
 *
 * @code
 * Point pt(3, 4);
 * std::reference_wrapper<Point> ref = std::ref(pt);
 * // toJs: creates a JS object sharing the same Point (borrowed, not copied)
 * JSValue jsRef = JsConverter<std::reference_wrapper<Point>>::toJs(ctx, ref);
 * // Modifications via JS are visible in pt, and vice versa.
 * @endcode
 */
template <typename T>
struct JsConverter<std::reference_wrapper<T>> {
    static JSValue toJs(JSContext* ctx, std::reference_wrapper<T> ref) {
        T* ptr = &ref.get();
        JSClassID cid = ClassRegistry::classId<T>();
        JSValue proto = JS_GetClassProto(ctx, cid);
        JSValue obj = JS_NewObjectProtoClass(ctx, proto, cid);
        JS_FreeValue(ctx, proto);
        auto* pd = ClassRegistry::makeBorrowed<T>(ptr);
        JS_SetOpaque(obj, pd);
        return obj;
    }
    static std::reference_wrapper<T> fromJs(JSContext* ctx, JSValueConst val) {
        JSClassID cid = ClassRegistry::classId<T>();
        auto* pd = static_cast<PointerData*>(JS_GetOpaque2(ctx, val, cid));
        T* ptr = pd ? pd->get<T>(ctx) : nullptr;
        assert(ptr && "fromJs<std::reference_wrapper<T>>: null native object");
        return std::ref(*ptr);
    }
};

// ============================================================================
// JSContext* passthrough (for callables that take ctx as parameter)
// ============================================================================

/**
 * @brief Specialization for JSContext* — pass the context directly.
 *
 * This allows callables like `void(JSContext*, int)` to receive the context
 * automatically without consuming a JS argument.
 */
template <>
struct JsConverter<JSContext*> {
    // Note: toJs is meaningless for JSContext*, but provided for completeness.
    static JSValue toJs(JSContext* /*ctx*/, JSContext* /*value*/) {
        return JS_UNDEFINED;
    }
    // fromJs is never called — JSContext* params are handled specially by the wrapper.
};

// ============================================================================
// Enum types (via underlying integer)
// ============================================================================

/**
 * @brief Specialization for enum types.
 *
 * Converts via the underlying integer type.
 */
template <typename T>
struct JsConverter<T, std::enable_if_t<std::is_enum_v<T>>> {
    using Underlying = std::underlying_type_t<T>;

    static JSValue toJs(JSContext* ctx, T value) {
        return JsConverter<Underlying>::toJs(ctx, static_cast<Underlying>(value));
    }
    static T fromJs(JSContext* ctx, JSValueConst value) {
        return static_cast<T>(JsConverter<Underlying>::fromJs(ctx, value));
    }
};

} // namespace qjsbind
