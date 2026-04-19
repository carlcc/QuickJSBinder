/**
 * @file function_wrapper.hpp
 * @brief Function wrapping, callable traits, and overload dispatch for QuickJSBinder.
 *
 * This header provides the template machinery to:
 * 1. Deduce the signature of any C++ callable (function pointer, member function
 *    pointer, lambda, std::function) via `callable_traits<F>`.
 * 2. Wrap any callable as a QuickJS `JSCClosure` callback that automatically
 *    converts JS arguments to C++ types, invokes the callable, and converts
 *    the return value back to JSValue.
 * 3. Wrap member-like callables (where the first parameter is T& or T*) for
 *    use as instance methods on a ClassBinder<T>-registered class.
 * 4. Support overload dispatch: multiple callables with different arity or
 *    parameter types, selected at runtime by argument count/type matching.
 *
 * @note Part of the QuickJSBinder header-only C++17 library.
 */
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <new>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "js_converter.hpp"
#include "pointer_data.hpp"

namespace qjsbind {

// ============================================================================
// callable_traits — deduce return type & parameter types for any callable
// ============================================================================

namespace detail {

/// Primary template (SFINAE fallback for operator()-based callables).
template <typename F, typename = void>
struct callable_traits_impl;

/// Specialization for plain function pointers.
template <typename R, typename... Args>
struct callable_traits_impl<R(*)(Args...)> {
    using return_type = R;
    using args_tuple = std::tuple<Args...>;
    static constexpr size_t arity = sizeof...(Args);
    static constexpr bool is_member_function = false;
    using class_type = void;
};

/// Specialization for member function pointers (non-const).
template <typename R, typename C, typename... Args>
struct callable_traits_impl<R(C::*)(Args...)> {
    using return_type = R;
    using args_tuple = std::tuple<Args...>;
    static constexpr size_t arity = sizeof...(Args);
    static constexpr bool is_member_function = true;
    using class_type = C;
};

/// Specialization for member function pointers (const).
template <typename R, typename C, typename... Args>
struct callable_traits_impl<R(C::*)(Args...) const> {
    using return_type = R;
    using args_tuple = std::tuple<Args...>;
    static constexpr size_t arity = sizeof...(Args);
    static constexpr bool is_member_function = true;
    using class_type = C;
};

/// Specialization for member function pointers (noexcept).
template <typename R, typename C, typename... Args>
struct callable_traits_impl<R(C::*)(Args...) noexcept> {
    using return_type = R;
    using args_tuple = std::tuple<Args...>;
    static constexpr size_t arity = sizeof...(Args);
    static constexpr bool is_member_function = true;
    using class_type = C;
};

/// Specialization for member function pointers (const noexcept).
template <typename R, typename C, typename... Args>
struct callable_traits_impl<R(C::*)(Args...) const noexcept> {
    using return_type = R;
    using args_tuple = std::tuple<Args...>;
    static constexpr size_t arity = sizeof...(Args);
    static constexpr bool is_member_function = true;
    using class_type = C;
};

/// Fallback: callable objects with operator() (lambdas, functors, std::function).
template <typename F>
struct callable_traits_impl<F, std::void_t<decltype(&std::decay_t<F>::operator())>>
    : callable_traits_impl<decltype(&std::decay_t<F>::operator())> {
    // Override: the class_type here is the functor itself, NOT a bound class.
    static constexpr bool is_member_function = false;
    using class_type = void;
};

} // namespace detail

/**
 * @brief Deduce the return type, parameter types, and arity of any callable.
 *
 * Supports function pointers, member function pointers, lambdas, functors,
 * and std::function. Provides:
 * - `return_type` : the return type R
 * - `args_tuple`  : std::tuple<Args...> of the parameter types
 * - `arity`       : number of parameters (size_t)
 * - `is_member_function` : true for member function pointers
 * - `class_type`  : the class C for member function pointers, void otherwise
 *
 * @tparam F The callable type.
 */
template <typename F>
struct callable_traits : detail::callable_traits_impl<std::decay_t<F>> {};

// ============================================================================
// Helper: get native T* from a JS this_val (supports inheritance)
// ============================================================================

namespace detail {

/**
 * @brief Retrieve the native T* pointer from a JS object's opaque data.
 *
 * First attempts JS_GetOpaque2 with T's class_id. If that fails (e.g.,
 * when a derived class calls a base class method), falls back to
 * JS_GetAnyOpaque which succeeds for any class_id.
 *
 * @tparam T The expected native type.
 * @param ctx JS context.
 * @param this_val The JS object.
 * @return Typed pointer, or nullptr (with pending TypeError).
 */
template <typename T>
T* getThisPointer(JSContext* ctx, JSValueConst this_val) {
    JSClassID cid = ClassIdHolder<T>::class_id;
    // Try exact class_id first.
    void* opaque = JS_GetOpaque2(ctx, this_val, cid);
    if (!opaque) {
        // Clear the pending exception from JS_GetOpaque2.
        if (JS_HasException(ctx)) {
            JSValue exc = JS_GetException(ctx);
            JS_FreeValue(ctx, exc);
        }
        // Fallback: accept any class_id (for inheritance).
        JSClassID actual_cid;
        opaque = JS_GetAnyOpaque(this_val, &actual_cid);
    }
    if (!opaque) {
        JS_ThrowTypeError(ctx, "invalid native object");
        return nullptr;
    }
    auto* pd = static_cast<PointerData*>(opaque);
    return pd->get<T>(ctx);
}

} // namespace detail

// ============================================================================
// Helpers: argument conversion from JS argv
// ============================================================================

namespace detail {

/// Check if a type is JSContext* (needs special handling — passed directly, not from argv).
template <typename T>
constexpr bool is_jscontext_ptr_v = std::is_same_v<std::decay_t<T>, JSContext*>;

/// Count how many JSContext* params appear in tuple indices [0, I).
template <typename ArgsTuple, size_t I>
constexpr int jsContextCountBefore() {
    if constexpr (I == 0) {
        return 0;
    } else {
        return jsContextCountBefore<ArgsTuple, I - 1>() +
               (is_jscontext_ptr_v<std::tuple_element_t<I - 1, ArgsTuple>> ? 1 : 0);
    }
}

/// Count total JSContext* params in the entire tuple.
template <typename ArgsTuple>
constexpr int jsContextCountTotal() {
    return jsContextCountBefore<ArgsTuple, std::tuple_size_v<ArgsTuple>>();
}

/// Convert the I-th parameter: JSContext* gets ctx; others get argv[adjusted_index].
template <typename ArgsTuple, size_t I>
decltype(auto) convertArgAt(JSContext* ctx, JSValueConst* argv, int argc) {
    using ParamT = std::tuple_element_t<I, ArgsTuple>;
    if constexpr (is_jscontext_ptr_v<ParamT>) {
        return ctx;
    } else {
        constexpr int jsIdx = static_cast<int>(I) - jsContextCountBefore<ArgsTuple, I>();
        if (jsIdx >= argc) {
            if constexpr (std::is_default_constructible_v<std::decay_t<ParamT>>) {
                return std::decay_t<ParamT>{};
            }
        }
        return JsConverter<std::decay_t<ParamT>>::fromJs(ctx, argv[jsIdx]);
    }
}

/// Convert all parameters, handling JSContext* specially.
/// Uses a decayed tuple for storage to avoid dangling references to temporaries.
template <typename ArgsTuple, size_t... Is>
auto convertArgs(JSContext* ctx, JSValueConst* argv, int argc,
                 std::index_sequence<Is...>) {
    // ArgsTuple may contain reference types (e.g. const Vec3&). We must store
    // by value to keep temporaries alive, then let applyMember/applyImpl
    // naturally bind the values to the callee's reference parameters.
    using StorageTuple = std::tuple<std::decay_t<std::tuple_element_t<Is, ArgsTuple>>...>;
    return StorageTuple{convertArgAt<ArgsTuple, Is>(ctx, argv, argc)...};
}

/// Invoke a callable with a tuple of arguments.
template <typename F, typename Tuple, size_t... Is>
auto applyImpl(F&& fn, Tuple&& args, std::index_sequence<Is...>)
    -> decltype(fn(std::get<Is>(std::forward<Tuple>(args))...))
{
    return fn(std::get<Is>(std::forward<Tuple>(args))...);
}

template <typename F, typename Tuple>
decltype(auto) invokeWithTuple(F&& fn, Tuple&& args) {
    constexpr size_t N = std::tuple_size_v<std::decay_t<Tuple>>;
    return applyImpl(std::forward<F>(fn), std::forward<Tuple>(args),
                     std::make_index_sequence<N>{});
}

/// Invoke a member function with (obj_ptr, tuple_of_args).
template <typename MemFn, typename Obj, typename Tuple, size_t... Is>
auto applyMemberImpl(MemFn fn, Obj* obj, Tuple&& args, std::index_sequence<Is...>)
    -> decltype((obj->*fn)(std::get<Is>(std::forward<Tuple>(args))...))
{
    return (obj->*fn)(std::get<Is>(std::forward<Tuple>(args))...);
}

template <typename MemFn, typename Obj, typename Tuple>
decltype(auto) applyMember(MemFn fn, Obj* obj, Tuple&& args) {
    constexpr size_t N = std::tuple_size_v<std::decay_t<Tuple>>;
    return applyMemberImpl(fn, obj, std::forward<Tuple>(args),
                           std::make_index_sequence<N>{});
}

} // namespace detail

// ============================================================================
// FunctionWrapper — wrap a free/static function as a JSCClosure
// ============================================================================

namespace detail {

/**
 * @brief Storage for a type-erased callable stored in a JSCClosure opaque.
 */
template <typename F>
struct CallableStorage {
    std::decay_t<F> fn;

    explicit CallableStorage(F&& f) : fn(std::forward<F>(f)) {}

    static void release(void* opaque) {
        delete static_cast<CallableStorage*>(opaque);
    }
};

} // namespace detail

/**
 * @brief Wrap a free/static function (or lambda, std::function) as a QuickJS closure.
 *
 * The callable is stored in the closure's opaque data. Arguments are
 * automatically converted from JSValue to C++ types, and the return value
 * is converted back.
 *
 * @tparam F Callable type.
 * @param ctx JS context.
 * @param name Function name (for debugging).
 * @param fn The callable to wrap.
 * @return JSValue representing the JS function (owned).
 */
template <typename F>
JSValue wrapFunction(JSContext* ctx, const char* name, F&& fn) {
    using Traits = callable_traits<F>;
    using ArgsTuple = typename Traits::args_tuple;
    using R = typename Traits::return_type;
    constexpr int arity = static_cast<int>(Traits::arity) -
                          detail::jsContextCountTotal<ArgsTuple>();

    auto* storage = new detail::CallableStorage<F>(std::forward<F>(fn));

    JSValue jsFn = JS_NewCClosure(
        ctx,
        // The JSCClosure callback:
        [](JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv,
           int /*magic*/, void* opaque) -> JSValue {
            auto* s = static_cast<detail::CallableStorage<std::decay_t<F>>*>(opaque);
            auto args = detail::convertArgs<ArgsTuple>(
                ctx, argv, argc, std::make_index_sequence<std::tuple_size_v<ArgsTuple>>{});
            if constexpr (std::is_void_v<R>) {
                detail::invokeWithTuple(s->fn, std::move(args));
                return JS_UNDEFINED;
            } else {
                R result = detail::invokeWithTuple(s->fn, std::move(args));
                return JsConverter<std::decay_t<R>>::toJs(ctx, result);
            }
        },
        name,
        &detail::CallableStorage<std::decay_t<F>>::release,
        arity,
        0, // magic
        storage);

    return jsFn;
}

// ============================================================================
// Method wrapper helpers — wrap a callable as an instance method
// ============================================================================

namespace detail {

/// Check if the first parameter of a callable is T& or T*.
template <typename T, typename ArgsTuple>
struct first_param_is_this : std::false_type {};

template <typename T, typename First, typename... Rest>
struct first_param_is_this<T, std::tuple<First, Rest...>> {
    using CleanFirst = std::remove_cv_t<std::remove_reference_t<First>>;
    // T& case: First is T& or const T&
    static constexpr bool is_ref =
        std::is_lvalue_reference_v<First> &&
        (std::is_same_v<CleanFirst, T> || std::is_base_of_v<CleanFirst, T>);
    // T* case: First is T* or const T*
    static constexpr bool is_ptr =
        std::is_pointer_v<First> &&
        (std::is_same_v<std::remove_cv_t<std::remove_pointer_t<First>>, T> ||
         std::is_base_of_v<std::remove_cv_t<std::remove_pointer_t<First>>, T>);
    static constexpr bool value = is_ref || is_ptr;
};

/// Get the "rest" args tuple (skip the first this-like param).
template <typename ArgsTuple>
struct rest_args;

template <typename First, typename... Rest>
struct rest_args<std::tuple<First, Rest...>> {
    using type = std::tuple<Rest...>;
};

template <>
struct rest_args<std::tuple<>> {
    using type = std::tuple<>;
};

/// Check if First is T& / const T&.
template <typename T, typename First>
constexpr bool is_this_ref_v =
    std::is_lvalue_reference_v<First> &&
    (std::is_same_v<std::remove_cv_t<std::remove_reference_t<First>>, T> ||
     std::is_base_of_v<std::remove_cv_t<std::remove_reference_t<First>>, T>);

/// Check if First is T* / const T*.
template <typename T, typename First>
constexpr bool is_this_ptr_v =
    std::is_pointer_v<First> &&
    (std::is_same_v<std::remove_cv_t<std::remove_pointer_t<First>>, T> ||
     std::is_base_of_v<std::remove_cv_t<std::remove_pointer_t<First>>, T>);

} // namespace detail

/**
 * @brief Wrap a callable as an instance method for ClassBinder<T>.
 *
 * Handles three cases:
 * 1. Member function pointer `R(T::*)(Args...)` — called on the native T* object.
 * 2. Non-member callable where first param is `T&` — native T* is dereferenced.
 * 3. Non-member callable where first param is `T*` — native T* is passed directly.
 *
 * The native T* is retrieved from `this_val` via JS_GetOpaque.
 *
 * @tparam T The class type.
 * @tparam F The callable type.
 * @param ctx JS context.
 * @param name Method name (for debugging).
 * @param fn The callable.
 * @param class_id JSClassID for T (used in JS_GetOpaque).
 * @return JSValue representing the JS method (owned).
 */
template <typename T, typename F>
JSValue wrapMethod(JSContext* ctx, const char* name, F&& fn, JSClassID class_id) {
    using Traits = callable_traits<F>;

    if constexpr (Traits::is_member_function) {
        // Case 1: member function pointer R(T::*)(Args...)
        static_assert(
            std::is_same_v<typename Traits::class_type, T> ||
            std::is_base_of_v<typename Traits::class_type, T>,
            "Member function class type must match or be a base of T");

        using ArgsTuple = typename Traits::args_tuple;
        using R = typename Traits::return_type;
        constexpr int arity = static_cast<int>(Traits::arity);

        auto* storage = new detail::CallableStorage<F>(std::forward<F>(fn));

        return JS_NewCClosure(
            ctx,
            [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv,
               int /*magic*/, void* opaque) -> JSValue {
                T* self = detail::getThisPointer<T>(ctx, this_val);
                if (!self) return JS_EXCEPTION;
                auto* s = static_cast<detail::CallableStorage<std::decay_t<F>>*>(opaque);
                auto args = detail::convertArgs<ArgsTuple>(
                    ctx, argv, argc, std::make_index_sequence<std::tuple_size_v<ArgsTuple>>{});
                if constexpr (std::is_void_v<R>) {
                    detail::applyMember(s->fn, self, std::move(args));
                    return JS_UNDEFINED;
                } else {
                    R result = detail::applyMember(s->fn, self, std::move(args));
                    return JsConverter<std::decay_t<R>>::toJs(ctx, result);
                }
            },
            name,
            &detail::CallableStorage<std::decay_t<F>>::release,
            arity, 0, storage);
    } else {
        // Case 2 & 3: non-member callable, first param is this (T& or T*).
        using ArgsTuple = typename Traits::args_tuple;
        using R = typename Traits::return_type;
        static_assert(std::tuple_size_v<ArgsTuple> > 0,
                      "Non-member method callable must have at least one parameter (this)");
        using FirstParam = std::tuple_element_t<0, ArgsTuple>;

        static_assert(
            detail::is_this_ref_v<T, FirstParam> || detail::is_this_ptr_v<T, FirstParam>,
            "First parameter of non-member method callable must be T& or T* "
            "(or const variants), where T is the ClassBinder target type");

        using RestTuple = typename detail::rest_args<ArgsTuple>::type;
        constexpr int js_arity = static_cast<int>(std::tuple_size_v<RestTuple>);

        auto* storage = new detail::CallableStorage<F>(std::forward<F>(fn));

        return JS_NewCClosure(
            ctx,
            [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv,
               int /*magic*/, void* opaque) -> JSValue {
                T* self = detail::getThisPointer<T>(ctx, this_val);
                if (!self) return JS_EXCEPTION;
                auto* s = static_cast<detail::CallableStorage<std::decay_t<F>>*>(opaque);

                // Convert rest args from JS.
                auto restArgs = detail::convertArgs<RestTuple>(
                    ctx, argv, argc,
                    std::make_index_sequence<std::tuple_size_v<RestTuple>>{});

                // Build full args tuple: prepend this.
                if constexpr (detail::is_this_ref_v<T, FirstParam>) {
                    auto fullArgs = std::tuple_cat(std::tie(*self), std::move(restArgs));
                    if constexpr (std::is_void_v<R>) {
                        detail::invokeWithTuple(s->fn, std::move(fullArgs));
                        return JS_UNDEFINED;
                    } else {
                        R result = detail::invokeWithTuple(s->fn, std::move(fullArgs));
                        return JsConverter<std::decay_t<R>>::toJs(ctx, result);
                    }
                } else {
                    // T* case
                    auto fullArgs = std::tuple_cat(std::make_tuple(self), std::move(restArgs));
                    if constexpr (std::is_void_v<R>) {
                        detail::invokeWithTuple(s->fn, std::move(fullArgs));
                        return JS_UNDEFINED;
                    } else {
                        R result = detail::invokeWithTuple(s->fn, std::move(fullArgs));
                        return JsConverter<std::decay_t<R>>::toJs(ctx, result);
                    }
                }
            },
            name,
            &detail::CallableStorage<std::decay_t<F>>::release,
            js_arity, 0, storage);
    }
}

// ============================================================================
// OverloadResolver — runtime dispatch across multiple callables
// ============================================================================

// ---------------------------------------------------------------------------
// Compile-time → runtime JS type matching
// ---------------------------------------------------------------------------

namespace detail {

/// Check if a JS value is compatible with C++ type T.
/// Default: accept anything (for types without a clear JS tag).
template <typename T, typename Enable = void>
struct JsTypeMatcher {
    static bool match(JSContext*, JSValueConst value) { return JS_GetClassID(value) == ClassRegistry::classId<T>(); }
};

template <typename T>
struct JsTypeMatcher<T*> {
    static bool match(JSContext*, JSValueConst value) { return JS_GetClassID(value) == ClassRegistry::classId<T>() || JS_IsNull(value);; }
};

/// bool — JS_TAG_BOOL
template <>
struct JsTypeMatcher<bool> {
    static bool match(JSContext*, JSValueConst v) { return JS_IsBool(v); }
};

/// Integer types (≤32-bit signed) — JS_TAG_INT or number without fractional part.
template <typename T>
struct JsTypeMatcher<T, std::enable_if_t<
    std::is_integral_v<T> && !std::is_same_v<T, bool> && (sizeof(T) <= 4)>> {
    static bool match(JSContext*, JSValueConst v) {
        return JS_IsNumber(v) && !JS_IsBool(v);
    }
};

/// 64-bit integers — also accept BigInt.
template <typename T>
struct JsTypeMatcher<T, std::enable_if_t<
    std::is_integral_v<T> && !std::is_same_v<T, bool> && (sizeof(T) > 4)>> {
    static bool match(JSContext*, JSValueConst v) {
        return JS_IsNumber(v) || JS_IsBigInt(v);
    }
};

/// Floating point — any JS number.
template <typename T>
struct JsTypeMatcher<T, std::enable_if_t<std::is_floating_point_v<T>>> {
    static bool match(JSContext*, JSValueConst v) {
        return JS_IsNumber(v);
    }
};

/// std::string — JS string.
template <>
struct JsTypeMatcher<std::string> {
    static bool match(JSContext*, JSValueConst v) { return JS_IsString(v); }
};

/// const char* — JS string.
template <>
struct JsTypeMatcher<const char*> {
    static bool match(JSContext*, JSValueConst v) { return JS_IsString(v); }
};

/// Enum types — same as their underlying integer.
template <typename T>
struct JsTypeMatcher<T, std::enable_if_t<std::is_enum_v<T>>> {
    static bool match(JSContext* ctx, JSValueConst v) {
        return JsTypeMatcher<std::underlying_type_t<T>>::match(ctx, v);
    }
};

/// std::function<...> — JS function.
template <typename R, typename... A>
struct JsTypeMatcher<std::function<R(A...)>> {
    static bool match(JSContext* ctx, JSValueConst v) {
        return JS_IsFunction(ctx, v);
    }
};

/// JsValue / JSValue — accept anything.
template <>
struct JsTypeMatcher<JsValue> {
    static bool match(JSContext*, JSValueConst) { return true; }
};
template <>
struct JsTypeMatcher<JSValue> {
    static bool match(JSContext*, JSValueConst) { return true; }
};

/// std::vector<T> — JS array.
template <typename T>
struct JsTypeMatcher<std::vector<T>> {
    static bool match(JSContext*, JSValueConst v) { return JS_IsArray(v); }
};

/// std::optional<T> — match inner type, or undefined/null.
template <typename T>
struct JsTypeMatcher<std::optional<T>> {
    static bool match(JSContext* ctx, JSValueConst v) {
        return JS_IsUndefined(v) || JS_IsNull(v) ||
               JsTypeMatcher<T>::match(ctx, v);
    }
};

// ---------------------------------------------------------------------------
// Build a type_check function from an ArgsTuple at compile time.
// ---------------------------------------------------------------------------

/// Check all argv[0..N) against their corresponding C++ parameter types.
template <typename ArgsTuple, size_t... Is>
bool matchArgTypes(JSContext* ctx, int argc, JSValueConst* argv,
                   std::index_sequence<Is...>) {
    constexpr int N = static_cast<int>(sizeof...(Is));
    if (argc != N) return false;
    return (JsTypeMatcher<std::decay_t<std::tuple_element_t<Is, ArgsTuple>>>
                ::match(ctx, argv[Is]) && ...);
}

/// Implementation detail — filters out JSContext* params, then builds checker.
template <typename ArgsTuple, size_t... Is>
auto makeTypeCheckerImpl(std::index_sequence<Is...>) {
    // Collect non-JSContext* param types into a new tuple for matching.
    // We do it the simple way: just iterate argv indices and check
    // the types that actually map to JS args.
    return [](JSContext* ctx, int argc, JSValueConst* argv) -> bool {
        // Build array of match-functions for JS-visible params only.
        using MatchFn = bool(*)(JSContext*, JSValueConst);
        constexpr size_t total = sizeof...(Is);
        // Determine which indices map to JS args (not JSContext*).
        MatchFn matchers[total > 0 ? total : 1] = {};
        int jsCount = 0;
        // Fold expression to fill matchers array.
        ((void)(is_jscontext_ptr_v<std::tuple_element_t<Is, ArgsTuple>>
            ? 0
            : (matchers[jsCount++] =
                   &JsTypeMatcher<std::decay_t<std::tuple_element_t<Is, ArgsTuple>>>::match, 0)
        ), ...);
        if (argc != jsCount) return false;
        for (int i = 0; i < jsCount; ++i) {
            if (!matchers[i](ctx, argv[i])) return false;
        }
        return true;
    };
}

/// Generate a type_check function pointer for a given ArgsTuple.
/// JSContext* params in ArgsTuple are skipped (they don't consume an argv slot).
template <typename ArgsTuple>
auto makeTypeChecker() {
    using Seq = std::make_index_sequence<std::tuple_size_v<ArgsTuple>>;
    return makeTypeCheckerImpl<ArgsTuple>(Seq{});
}

} // namespace detail

/**
 * @brief A single entry in the overload table.
 *
 * Each entry stores a wrapped JSCClosure callback, expected parameter count,
 * and a type_check function that returns true when the JS arguments are
 * compatible with this overload's C++ parameter types.
 */
struct OverloadEntry {
    /// The actual wrapped function (a JSCClosure callback).
    JSValue (*invoke)(JSContext* ctx, JSValueConst this_val,
                      int argc, JSValueConst* argv, void* data);
    /// Opaque data for the invoke callback (CallableStorage pointer).
    void* data;
    /// Destructor for the opaque data.
    void (*data_dtor)(void*);
    /// Expected number of JS arguments (-1 = any).
    int expected_argc;
    /// Type-level argument matcher (nullptr = no type check, argc-only).
    bool (*type_check)(JSContext* ctx, int argc, JSValueConst* argv);
};

/**
 * @brief Overload dispatch table stored in a JSCClosure opaque.
 *
 * Dispatch order:
 *  1. Entries with type_check: argc AND type match.
 *  2. Entries without type_check: argc match only.
 *  3. Entries with expected_argc == -1 (variadic catch-all).
 */
struct OverloadTable {
    std::vector<OverloadEntry> entries;

    ~OverloadTable() {
        for (auto& e : entries) {
            if (e.data && e.data_dtor) {
                e.data_dtor(e.data);
            }
        }
    }
};

namespace detail {

/// JSCClosure callback that dispatches to the first matching overload.
inline JSValue overloadDispatcher(JSContext* ctx, JSValueConst this_val,
                                  int argc, JSValueConst* argv,
                                  int /*magic*/, void* opaque) {
    auto* table = static_cast<OverloadTable*>(opaque);

    // Pass 1: exact match — argc AND type_check both satisfied.
    for (auto& entry : table->entries) {
        if (entry.type_check && entry.type_check(ctx, argc, argv)) {
            return entry.invoke(ctx, this_val, argc, argv, entry.data);
        }
    }
    // Pass 2: argc-only match (entries without type_check).
    for (auto& entry : table->entries) {
        if (!entry.type_check &&
            entry.expected_argc != -1 &&
            entry.expected_argc == argc) {
            return entry.invoke(ctx, this_val, argc, argv, entry.data);
        }
    }
    // Pass 3: variadic catch-all (expected_argc == -1).
    for (auto& entry : table->entries) {
        if (entry.expected_argc == -1) {
            return entry.invoke(ctx, this_val, argc, argv, entry.data);
        }
    }
    return JS_ThrowTypeError(ctx, "no matching overload for %d argument(s)", argc);
}

/// Opaque finalizer for OverloadTable.
inline void overloadTableFinalizer(void* opaque) {
    delete static_cast<OverloadTable*>(opaque);
}

/// Create an OverloadEntry from a free/static callable for use in overload dispatch.
template <typename F>
OverloadEntry makeFreeFunctionEntry(F&& fn) {
    using Traits = callable_traits<F>;
    using ArgsTuple = typename Traits::args_tuple;
    using R = typename Traits::return_type;

    auto* storage = new CallableStorage<F>(std::forward<F>(fn));

    OverloadEntry entry;
    entry.expected_argc = static_cast<int>(Traits::arity);
    entry.data = storage;
    entry.data_dtor = &CallableStorage<std::decay_t<F>>::release;
    entry.type_check = makeTypeChecker<ArgsTuple>();
    entry.invoke = [](JSContext* ctx, JSValueConst /*this_val*/,
                      int argc, JSValueConst* argv, void* data) -> JSValue {
        auto* s = static_cast<CallableStorage<std::decay_t<F>>*>(data);
        auto args = convertArgs<ArgsTuple>(
            ctx, argv, argc, std::make_index_sequence<std::tuple_size_v<ArgsTuple>>{});
        if constexpr (std::is_void_v<R>) {
            invokeWithTuple(s->fn, std::move(args));
            return JS_UNDEFINED;
        } else {
            R result = invokeWithTuple(s->fn, std::move(args));
            return JsConverter<std::decay_t<R>>::toJs(ctx, result);
        }
    };
    return entry;
}

/// Create an OverloadEntry for a member function pointer.
template <typename T, typename MemFn>
OverloadEntry makeMemberFunctionEntry(MemFn fn) {
    using Traits = callable_traits<MemFn>;
    using ArgsTuple = typename Traits::args_tuple;
    using R = typename Traits::return_type;

    auto* storage = new CallableStorage<MemFn>(std::move(fn));

    OverloadEntry entry;
    entry.expected_argc = static_cast<int>(Traits::arity);
    entry.data = storage;
    entry.data_dtor = &CallableStorage<std::decay_t<MemFn>>::release;
    entry.type_check = makeTypeChecker<ArgsTuple>();
    entry.invoke = [](JSContext* ctx, JSValueConst this_val,
                      int argc, JSValueConst* argv, void* data) -> JSValue {
        T* self = getThisPointer<T>(ctx, this_val);
        if (!self) return JS_EXCEPTION;
        auto* s = static_cast<CallableStorage<std::decay_t<MemFn>>*>(data);
        auto args = convertArgs<ArgsTuple>(
            ctx, argv, argc, std::make_index_sequence<std::tuple_size_v<ArgsTuple>>{});
        if constexpr (std::is_void_v<R>) {
            applyMember(s->fn, self, std::move(args));
            return JS_UNDEFINED;
        } else {
            R result = applyMember(s->fn, self, std::move(args));
            return JsConverter<std::decay_t<R>>::toJs(ctx, result);
        }
    };
    return entry;
}

/// Create an OverloadEntry for a non-member callable used as an instance method (first param = this).
template <typename T, typename F>
OverloadEntry makeThisCallableEntry(F&& fn) {
    using Traits = callable_traits<F>;
    using ArgsTuple = typename Traits::args_tuple;
    using R = typename Traits::return_type;
    using FirstParam = std::tuple_element_t<0, ArgsTuple>;
    using RestTuple = typename rest_args<ArgsTuple>::type;

    auto* storage = new CallableStorage<F>(std::forward<F>(fn));

    OverloadEntry entry;
    entry.expected_argc = static_cast<int>(std::tuple_size_v<RestTuple>);
    entry.data = storage;
    entry.data_dtor = &CallableStorage<std::decay_t<F>>::release;
    entry.type_check = makeTypeChecker<RestTuple>();
    entry.invoke = [](JSContext* ctx, JSValueConst this_val,
                      int argc, JSValueConst* argv, void* data) -> JSValue {
        T* self = getThisPointer<T>(ctx, this_val);
        if (!self) return JS_EXCEPTION;
        auto* s = static_cast<CallableStorage<std::decay_t<F>>*>(data);
        auto restArgs = convertArgs<RestTuple>(
            ctx, argv, argc,
            std::make_index_sequence<std::tuple_size_v<RestTuple>>{});

        if constexpr (is_this_ref_v<T, FirstParam>) {
            auto fullArgs = std::tuple_cat(std::tie(*self), std::move(restArgs));
            if constexpr (std::is_void_v<R>) {
                invokeWithTuple(s->fn, std::move(fullArgs));
                return JS_UNDEFINED;
            } else {
                R result = invokeWithTuple(s->fn, std::move(fullArgs));
                return JsConverter<std::decay_t<R>>::toJs(ctx, result);
            }
        } else {
            auto fullArgs = std::tuple_cat(std::make_tuple(self), std::move(restArgs));
            if constexpr (std::is_void_v<R>) {
                invokeWithTuple(s->fn, std::move(fullArgs));
                return JS_UNDEFINED;
            } else {
                R result = invokeWithTuple(s->fn, std::move(fullArgs));
                return JsConverter<std::decay_t<R>>::toJs(ctx, result);
            }
        }
    };
    return entry;
}

/// Create the appropriate OverloadEntry for a callable used as an instance method on T.
template <typename T, typename F>
OverloadEntry makeMethodEntry(F&& fn) {
    using Traits = callable_traits<F>;
    if constexpr (Traits::is_member_function) {
        return makeMemberFunctionEntry<T>(std::forward<F>(fn));
    } else {
        using ArgsTuple = typename Traits::args_tuple;
        static_assert(std::tuple_size_v<ArgsTuple> > 0,
                      "Non-member method callable must have at least one parameter (this)");
        using FirstParam = std::tuple_element_t<0, ArgsTuple>;
        static_assert(
            is_this_ref_v<T, FirstParam> || is_this_ptr_v<T, FirstParam>,
            "First parameter of non-member method callable must be T& or T*");
        return makeThisCallableEntry<T>(std::forward<F>(fn));
    }
}

/// Recursively add OverloadEntries for each callable in a parameter pack.
template <typename T>
void addMethodEntries(OverloadTable& /*table*/) {
    // Base case: no more callables.
}

template <typename T, typename F, typename... Rest>
void addMethodEntries(OverloadTable& table, F&& fn, Rest&&... rest) {
    table.entries.push_back(makeMethodEntry<T>(std::forward<F>(fn)));
    addMethodEntries<T>(table, std::forward<Rest>(rest)...);
}

} // namespace detail

/**
 * @brief Create a JS function from an OverloadTable.
 *
 * @param ctx JS context.
 * @param name Function name (for debugging/stack traces).
 * @param table Heap-allocated OverloadTable (ownership transferred to the closure).
 * @return JSValue representing the JS function (owned).
 */
inline JSValue createOverloadedFunction(JSContext* ctx, const char* name,
                                        OverloadTable* table) {
    return JS_NewCClosure(
        ctx,
        detail::overloadDispatcher,
        name,
        detail::overloadTableFinalizer,
        0, // length (variadic)
        0, // magic
        table);
}

} // namespace qjsbind
