/**
 * @file class_binder.hpp
 * @brief Core class binding facility for QuickJSBinder.
 *
 * ClassBinder<T, Base> provides a fluent API to register C++ classes as
 * JavaScript constructable objects in QuickJS. Features include:
 *
 * - **constructor<Sigs...>()**: register native constructors (new T(Args...))
 * - **constructor2(callables...)**: register factory constructors (C functions,
 *   lambdas, etc. returning T* or T&)
 * - **destructor(callable)**: custom destructor (replaces default delete)
 * - **method("name", fns...)**: instance methods with overload support
 * - **static_method("name", fns...)**: static methods
 * - **property("name", getter, setter)**: instance properties
 * - **property_readonly("name", getter)**: read-only properties
 * - **enum_value("name", value)**: enum constants
 * - **install()**: finalize and register into the global object
 *
 * @note Part of the QuickJSBinder header-only C++17 library.
 */
#pragma once

#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "function_wrapper.hpp"
#include "js_converter.hpp"
#include "pointer_data.hpp"

namespace qjsbind {

// ============================================================================
// Constructor helpers
// ============================================================================

namespace detail {

/**
 * @brief Construct a native object via `new T(Args...)` and wrap it.
 * @tparam T Class type.
 * @tparam Args Constructor argument types.
 */
template <typename T, typename... Args>
struct NativeConstructor {
    static OverloadEntry makeEntry() {
        OverloadEntry entry;
        entry.expected_argc = static_cast<int>(sizeof...(Args));
        entry.data = nullptr;
        entry.data_dtor = nullptr;
        entry.invoke = [](JSContext* ctx, JSValueConst this_val,
                          int argc, JSValueConst* argv, void* /*data*/) -> JSValue {
            using ArgsTuple = std::tuple<Args...>;
            auto args = convertArgs<ArgsTuple>(
                ctx, argv, argc, std::make_index_sequence<sizeof...(Args)>{});
            T* obj = applyImpl(
                [](auto&&... a) -> T* { return new T(std::forward<decltype(a)>(a)...); },
                std::move(args),
                std::make_index_sequence<sizeof...(Args)>{});
            return JS_UNDEFINED; // Placeholder — actual object creation handled by ClassBinder.
        };
        return entry;
    }
};

/// Parse a constructor signature: void(Args...) → NativeConstructor.
template <typename T, typename Sig>
struct CtorSigParser;

template <typename T, typename... Args>
struct CtorSigParser<T, void(Args...)> {
    static OverloadEntry makeEntry() {
        OverloadEntry entry;
        entry.expected_argc = static_cast<int>(sizeof...(Args));
        entry.data = nullptr;
        entry.data_dtor = nullptr;
        entry.invoke = [](JSContext* ctx, JSValueConst /*this_val*/,
                          int argc, JSValueConst* argv, void* /*data*/) -> JSValue {
            // This will be called from the constructor dispatcher.
            // It creates the native object and returns a pointer encoded as JS int.
            // (The dispatcher wraps it in a proper JS object.)
            using ArgsTuple = std::tuple<std::decay_t<Args>...>;
            auto args = convertArgs<ArgsTuple>(
                ctx, argv, argc, std::make_index_sequence<sizeof...(Args)>{});
            T* obj = applyImpl(
                [](auto&&... a) -> T* { return new T(std::forward<decltype(a)>(a)...); },
                std::move(args),
                std::make_index_sequence<sizeof...(Args)>{});
            // Encode the pointer in the return value via a tag.
            // We use JS_MKPTR with a private tag — but that's not safe.
            // Instead, store the pointer in the ConstructorResult.
            // The dispatcher will extract it.
            return JS_NewInt64(ctx, reinterpret_cast<int64_t>(obj));
        };
        return entry;
    }
};

/// Recursive helper to add OverloadEntries for each ctor signature.
template <typename T>
void addCtorEntries(OverloadTable& /*table*/) {}

template <typename T, typename Sig, typename... Rest>
void addCtorEntries(OverloadTable& table) {
    table.entries.push_back(CtorSigParser<T, Sig>::makeEntry());
    addCtorEntries<T, Rest...>(table);
}

/// Create an OverloadEntry for a factory callable used as constructor2.
template <typename T, typename F>
OverloadEntry makeFactoryCtorEntry(F&& fn) {
    using Traits = callable_traits<F>;
    using ArgsTuple = typename Traits::args_tuple;
    using R = typename Traits::return_type;

    // Validate return type: must be T* or T& (or compatible).
    static_assert(
        std::is_pointer_v<R> || std::is_lvalue_reference_v<R>,
        "Factory constructor must return T* or T&");
    using CleanR = std::remove_cv_t<std::remove_reference_t<std::remove_pointer_t<R>>>;
    static_assert(
        std::is_same_v<CleanR, T> || std::is_base_of_v<T, CleanR>,
        "Factory constructor return type must be T* or T&");

    auto* storage = new CallableStorage<F>(std::forward<F>(fn));

    OverloadEntry entry;
    entry.expected_argc = static_cast<int>(Traits::arity);
    entry.data = storage;
    entry.data_dtor = &CallableStorage<std::decay_t<F>>::release;
    entry.invoke = [](JSContext* ctx, JSValueConst /*this_val*/,
                      int argc, JSValueConst* argv, void* data) -> JSValue {
        auto* s = static_cast<CallableStorage<std::decay_t<F>>*>(data);
        auto args = convertArgs<ArgsTuple>(
            ctx, argv, argc, std::make_index_sequence<std::tuple_size_v<ArgsTuple>>{});
        if constexpr (std::is_pointer_v<R>) {
            T* obj = invokeWithTuple(s->fn, std::move(args));
            return JS_NewInt64(ctx, reinterpret_cast<int64_t>(obj));
        } else {
            // Reference return — take address.
            T& ref = invokeWithTuple(s->fn, std::move(args));
            return JS_NewInt64(ctx, reinterpret_cast<int64_t>(&ref));
        }
    };
    return entry;
}

/// Recursively add factory ctor entries.
template <typename T>
void addFactoryCtorEntries(OverloadTable& /*table*/) {}

template <typename T, typename F, typename... Rest>
void addFactoryCtorEntries(OverloadTable& table, F&& fn, Rest&&... rest) {
    table.entries.push_back(makeFactoryCtorEntry<T>(std::forward<F>(fn)));
    addFactoryCtorEntries<T>(table, std::forward<Rest>(rest)...);
}

} // namespace detail

// ============================================================================
// ClassBinder<T, Base>
// ============================================================================

/**
 * @brief Fluent API for binding a C++ class T to JavaScript.
 *
 * Usage:
 * @code
 * ClassBinder<MyClass> binder(ctx, "MyClass");
 * binder.constructor<void(int), void(int, std::string)>()
 *       .method("doStuff", &MyClass::doStuff)
 *       .property("name", &MyClass::getName, &MyClass::setName)
 *       .install();
 * @endcode
 *
 * @tparam T The C++ class to bind.
 * @tparam Base Optional base class (for inheritance chains).
 */
template <typename T, typename Base = void>
class ClassBinder {
public:
    /**
     * @brief Construct a ClassBinder for type T.
     *
     * Allocates a JSClassID for T and registers the JSClassDef with the
     * runtime. Sets up the prototype and constructor objects.
     *
     * @param ctx JS context.
     * @param name The JavaScript class name.
     */
    ClassBinder(JSContext* ctx, const char* name)
        : ctx_(ctx)
        , rt_(JS_GetRuntime(ctx))
        , name_(name)
        , ctor_table_(new OverloadTable())
        , custom_dtor_(nullptr)
    {
        // Allocate class ID and register class.
        class_id_ = ClassRegistry::classId<T>(rt_);

        JSClassDef def{};
        def.class_name = name;
        def.finalizer = &finalizerCallback;
        def.gc_mark = &gcMarkCallback;
        ClassRegistry::registerClass<T>(rt_, &def);

        // Create prototype object.
        proto_ = JS_NewObject(ctx_);
    }

    /// Destructor: free any unreleased JS values.
    ~ClassBinder() {
        if (!installed_) {
            JS_FreeValue(ctx_, proto_);
            delete ctor_table_;
            for (auto& sp : static_props_) {
                JS_FreeValue(ctx_, sp.getter);
                if (!JS_IsUndefined(sp.setter))
                    JS_FreeValue(ctx_, sp.setter);
            }
        }
    }

    ClassBinder(const ClassBinder&) = delete;
    ClassBinder& operator=(const ClassBinder&) = delete;

    JSContext* context() const { return ctx_; }

    // -----------------------------------------------------------------------
    // Constructor registration
    // -----------------------------------------------------------------------

    /**
     * @brief Register native constructors via signature list.
     *
     * Each signature must be `void(Args...)`. The library will call
     * `new T(Args...)` for each matching overload.
     *
     * @code
     * binder.constructor<void(int), void(int, std::string)>();
     * @endcode
     *
     * Can be called multiple times; entries accumulate in the overload table.
     *
     * @tparam Sigs Signature types (e.g. void(int), void(int, bool)).
     * @return Reference to this ClassBinder for chaining.
     */
    template <typename... Sigs>
    ClassBinder& constructor() {
        detail::addCtorEntries<T, Sigs...>(*ctor_table_);
        return *this;
    }

    /**
     * @brief Register non-native (factory) constructors.
     *
     * Each callable must return `T*` or `T&`. The callable's parameters
     * become the JS constructor's parameters. Can pass multiple callables
     * to register as overloads.
     *
     * @code
     * binder.constructor2(
     *     [](int id) -> MyHandle* { return c_api_create(id); },
     *     &some_factory_function
     * );
     * @endcode
     *
     * Can be called multiple times; entries accumulate in the overload table.
     * Shares the overload table with constructor(), so all overloads are
     * dispatched uniformly at runtime.
     *
     * @tparam Factories Callable types returning T* or T&.
     * @param factories The factory callables.
     * @return Reference to this ClassBinder for chaining.
     */
    template <typename... Factories>
    ClassBinder& constructor2(Factories&&... factories) {
        detail::addFactoryCtorEntries<T>(*ctor_table_,
                                         std::forward<Factories>(factories)...);
        return *this;
    }

    /**
     * @brief Register a custom destructor (replaces default `delete ptr`).
     *
     * @code
     * binder.destructor([](MyHandle* p) { c_api_destroy(p); });
     * @endcode
     *
     * @tparam Dtor Callable type taking T*.
     * @param dtor The destructor callable.
     * @return Reference to this ClassBinder for chaining.
     */
    template <typename Dtor>
    ClassBinder& destructor(Dtor&& dtor) {
        custom_dtor_ = std::make_shared<std::function<void(T*)>>(
            std::forward<Dtor>(dtor));
        return *this;
    }

    // -----------------------------------------------------------------------
    // Method registration
    // -----------------------------------------------------------------------

    /**
     * @brief Register one or more instance methods under the same name.
     *
     * Each callable can be:
     * - A member function pointer `&T::func`
     * - A free function / C function / static member (first param T& or T*)
     * - A lambda / std::function (first param T& or T*)
     *
     * Multiple callables are registered as overloads dispatched by argument count.
     *
     * @code
     * binder.method("doStuff", &MyClass::doStuff);
     * binder.method("process", &MyClass::processInt, &MyClass::processStr);
     * binder.method("helper", [](MyClass& self, int x) { ... });
     * @endcode
     *
     * @tparam Fns Callable types.
     * @param name JS method name.
     * @param fns The callables.
     * @return Reference to this ClassBinder for chaining.
     */
    template <typename... Fns>
    ClassBinder& method(const char* name, Fns&&... fns) {
        if constexpr (sizeof...(Fns) == 1) {
            // Single callable — no overload table needed.
            JSValue fn = wrapMethod<T>(ctx_, name,
                                       std::forward<Fns>(fns)..., class_id_);
            JS_DefinePropertyValueStr(ctx_, proto_, name, fn,
                                      JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
        } else {
            // Multiple callables — build overload table.
            auto* table = new OverloadTable();
            detail::addMethodEntries<T>(*table, std::forward<Fns>(fns)...);
            JSValue fn = createOverloadedFunction(ctx_, name, table);
            JS_DefinePropertyValueStr(ctx_, proto_, name, fn,
                                      JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
        }
        return *this;
    }

    /**
     * @brief Register one or more static methods under the same name.
     *
     * Static methods do NOT receive a `this` object. Callables are wrapped
     * as plain functions (no this-binding).
     *
     * @tparam Fns Callable types.
     * @param name JS method name.
     * @param fns The callables.
     * @return Reference to this ClassBinder for chaining.
     */
    template <typename... Fns>
    ClassBinder& static_method(const char* name, Fns&&... fns) {
        static_methods_.push_back({name, {}});
        auto& entry = static_methods_.back();
        (entry.second.push_back(wrapFunction(ctx_, name, std::forward<Fns>(fns))), ...);
        return *this;
    }

    // -----------------------------------------------------------------------
    // Property registration
    // -----------------------------------------------------------------------

    /**
     * @brief Register an instance property with getter and setter.
     *
     * Getter and setter can be member function pointers, free functions,
     * or lambdas (with T& or T* first param treated as this).
     *
     * @code
     * binder.property("name", &MyClass::getName, &MyClass::setName);
     * binder.property("value",
     *     [](const MyClass& self) { return self.val; },
     *     [](MyClass& self, int v) { self.val = v; });
     * @endcode
     *
     * @tparam Getter Getter callable type.
     * @tparam Setter Setter callable type.
     * @param name JS property name.
     * @param getter The getter callable.
     * @param setter The setter callable.
     * @return Reference to this ClassBinder for chaining.
     */
    template <typename Getter, typename Setter>
    ClassBinder& property(const char* name, Getter&& getter, Setter&& setter) {
        JSValue jsGetter = wrapMethod<T>(ctx_, name, std::forward<Getter>(getter), class_id_);
        JSValue jsSetter = wrapMethod<T>(ctx_, name, std::forward<Setter>(setter), class_id_);

        JSAtom atom = JS_NewAtom(ctx_, name);
        JS_DefinePropertyGetSet(ctx_, proto_, atom, jsGetter, jsSetter,
                                JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
        JS_FreeAtom(ctx_, atom);
        return *this;
    }

    /**
     * @brief Register a read-only instance property.
     *
     * @tparam Getter Getter callable type.
     * @param name JS property name.
     * @param getter The getter callable.
     * @return Reference to this ClassBinder for chaining.
     */
    template <typename Getter>
    ClassBinder& property_readonly(const char* name, Getter&& getter) {
        JSValue jsGetter = wrapMethod<T>(ctx_, name, std::forward<Getter>(getter), class_id_);

        JSAtom atom = JS_NewAtom(ctx_, name);
        JS_DefinePropertyGetSet(ctx_, proto_, atom, jsGetter, JS_UNDEFINED,
                                JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
        JS_FreeAtom(ctx_, atom);
        return *this;
    }

    // -----------------------------------------------------------------------
    // Member-pointer based property registration
    // -----------------------------------------------------------------------

    /**
     * @brief Register a read-write property via a pointer-to-data-member.
     *
     * Automatically generates a getter and setter that directly access the
     * member variable. This is the most concise way to expose a public
     * data member.
     *
     * @code
     * struct Rect { double width, height; };
     * binder.property("width", &Rect::width);
     * binder.property("height", &Rect::height);
     * @endcode
     *
     * @tparam M Member type (deduced from the pointer-to-member).
     * @param name JS property name.
     * @param mp Pointer-to-data-member of T.
     * @return Reference to this ClassBinder for chaining.
     */
    template <typename M, std::enable_if_t<!std::is_function_v<M>, int> = 0>
    ClassBinder& property(const char* name, M T::* mp) {
        using DecayT = std::decay_t<M>;
        if constexpr (std::is_arithmetic_v<DecayT> || std::is_same_v<DecayT, bool> || std::is_enum_v<DecayT>) {
            auto getter = [mp](const T& self) -> M { return self.*mp; };
            auto setter = [mp](T& self, M val) { self.*mp = val; };
            return property(name, std::move(getter), std::move(setter));
        } else if constexpr (std::is_pointer_v<DecayT> || std::is_reference_v<DecayT>) {
            auto getter = [mp](const T& self) -> M { return self.*mp; };
            auto setter = [mp](T& self, M val) { self.*mp = val; };
            return property(name, std::move(getter), std::move(setter));
        } else {
            auto getter = [mp](const T& self) -> const M& { return self.*mp; };
            auto setter = [mp](T& self, const M& val) { self.*mp = val; };
            return property(name, std::move(getter), std::move(setter));
        }
    }

    /**
     * @brief Register a read-only property via a pointer-to-data-member.
     *
     * @code
     * struct Info { std::string name; int id; };
     * binder.property_readonly("id", &Info::id);
     * @endcode
     *
     * @tparam M Member type (deduced from the pointer-to-member).
     * @param name JS property name.
     * @param mp Pointer-to-data-member of T.
     * @return Reference to this ClassBinder for chaining.
     */
    template <typename M, std::enable_if_t<!std::is_function_v<M>, int> = 0>
    ClassBinder& property_readonly(const char* name, M T::* mp) {
        using DecayT = std::decay_t<M>;
        if constexpr (std::is_arithmetic_v<DecayT> || std::is_same_v<DecayT, bool> || std::is_enum_v<DecayT>) {
            auto getter = [mp](const T& self) -> M { return self.*mp; };
            return property_readonly(name, std::move(getter));
        } else if constexpr (std::is_pointer_v<DecayT> || std::is_reference_v<DecayT>) {
            auto getter = [mp](const T& self) -> M { return self.*mp; };
            return property_readonly(name, std::move(getter));
        } else {
            auto getter = [mp](const T& self) -> std::reference_wrapper<const M> { return self.*mp; };
            return property_readonly(name, std::move(getter));
        }
    }

    // -----------------------------------------------------------------------
    // Static property registration
    // -----------------------------------------------------------------------

    /**
     * @brief Register a read-write static property with getter and setter.
     *
     * Getter/setter are free functions or lambdas (no this parameter).
     * The property is installed on the constructor object (e.g. `MyClass.prop`).
     *
     * @code
     * binder.static_property("count",
     *     []() -> int { return MyClass::count; },
     *     [](int v) { MyClass::count = v; });
     * @endcode
     */
    template <typename Getter, typename Setter>
    ClassBinder& static_property(const char* name, Getter&& getter, Setter&& setter) {
        JSValue jsGetter = wrapFunction(ctx_, name, std::forward<Getter>(getter));
        JSValue jsSetter = wrapFunction(ctx_, name, std::forward<Setter>(setter));
        static_props_.push_back({name, jsGetter, jsSetter});
        return *this;
    }

    /**
     * @brief Register a read-only static property with getter.
     *
     * @code
     * binder.static_property_readonly("version",
     *     []() -> std::string { return "1.0"; });
     * @endcode
     */
    template <typename Getter>
    ClassBinder& static_property_readonly(const char* name, Getter&& getter) {
        JSValue jsGetter = wrapFunction(ctx_, name, std::forward<Getter>(getter));
        static_props_.push_back({name, jsGetter, JS_UNDEFINED});
        return *this;
    }

    /**
     * @brief Register a read-write static property bound to a native pointer.
     *
     * JS and C++ share the same memory — reads/writes go through the pointer
     * directly. No copy is made.
     *
     * @code
     * static int g_counter = 0;
     * binder.static_property("counter", &g_counter);
     * @endcode
     *
     * @tparam V Value type (deduced from the pointer).
     * @param name JS property name.
     * @param ptr Pointer to the native variable (must outlive the JS context).
     */
    template <typename V>
    ClassBinder& static_property(const char* name, V* ptr) {
        auto getter = [ptr]() -> const V& { return *ptr; };
        auto setter = [ptr](const V& val) { *ptr = val; };
        return static_property(name, std::move(getter), std::move(setter));
    }

    /**
     * @brief Register a read-only static property bound to a native pointer.
     *
     * @code
     * static const std::string kVersion = "2.0";
     * binder.static_property_readonly("version", &kVersion);
     * @endcode
     *
     * @tparam V Value type (deduced from the pointer).
     * @param name JS property name.
     * @param ptr Pointer to the native variable (must outlive the JS context).
     */
    template <typename V>
    ClassBinder& static_property_readonly(const char* name, const V* ptr) {
        auto getter = [ptr]() -> const V& { return *ptr; };
        return static_property_readonly(name, std::move(getter));
    }

    // -----------------------------------------------------------------------
    // Enum registration
    // -----------------------------------------------------------------------

    /**
     * @brief Register an enum constant as a property on the constructor object.
     *
     * @code
     * binder.enum_value("RED", Color::Red);
     * @endcode
     *
     * @tparam EnumT Enum type.
     * @param name Property name in JS.
     * @param value The enum value.
     * @return Reference to this ClassBinder for chaining.
     */
    template <typename EnumT>
    ClassBinder& enum_value(const char* name, EnumT value) {
        enum_values_.push_back({name, JsConverter<EnumT>::toJs(ctx_, value)});
        return *this;
    }

    // -----------------------------------------------------------------------
    // Installation
    // -----------------------------------------------------------------------

    /**
     * @brief Finalize and install the class into the global JS object.
     *
     * After this call, the class is available in JS as `new ClassName(...)`.
     * This method should be called exactly once after all method/property/enum
     * registrations are complete.
     */
    void install() {
        JSValue global = JS_GetGlobalObject(ctx_);
        installInto(global);
        JS_FreeValue(ctx_, global);
    }

    /**
     * @brief Finalize and install the class as a named export of a native ES module.
     *
     * This avoids the clunky pattern of creating a temporary object,
     * calling installInto(), and extracting the constructor property.
     * Instead, it directly builds the constructor and exports it
     * via JS_SetModuleExport.
     *
     * @code
     * // Inside a module init callback:
     * NativeModuleExport mod(ctx, m);
     * ClassBinder<Vec3> binder(ctx, "Vec3");
     * binder.constructor<void(), void(double, double, double)>()
     *       .method("length", &Vec3::length)
     *       .installAsModuleExport(mod);
     * @endcode
     *
     * @param mod The NativeModuleExport builder (must be in scope).
     */
    void installAsModuleExport(class NativeModuleExport& mod);

    /**
     * @brief Finalize and install the class into a specific JS object.
     *
     * Use this to install the class as a property of a module or namespace
     * object instead of the global object.
     *
     * @code
     * JsModule mod(ctx, "myLib");
     * ClassBinder<MyClass> binder(ctx, "MyClass");
     * binder.constructor<void()>()
     *       .method("doStuff", &MyClass::doStuff)
     *       .installInto(mod.object());
     * mod.install();
     * // JS: let obj = new myLib.MyClass();
     * @endcode
     *
     * @param target The JS object to install the constructor into (borrowed).
     */
    void installInto(JSValueConst target) {
        JSValue ctorFunc = buildConstructor();

        // Install constructor into the target object.
        JS_DefinePropertyValueStr(ctx_, target, name_.c_str(), ctorFunc,
                                  JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    }

private:

    /**
     * @brief Build the constructor function, set up prototype, inheritance,
     *        static methods, and enum values.
     *
     * This is the shared implementation for install(), installInto(), and
     * installAsModuleExport(). Returns a JSValue constructor function
     * with ownership transferred to the caller.
     */
    JSValue buildConstructor() {
        assert(!installed_ && "ClassBinder: install/installInto/installAsModuleExport called twice");
        installed_ = true;

        // Create the constructor function.
        JSValue ctorFunc = JS_NewCClosure(
            ctx_,
            &constructorCallback,
            name_.c_str(),
            &ctorClosureFinalizer,
            0, 0,
            new CtorClosureData{ctor_table_, custom_dtor_, class_id_});
        ctor_table_ = nullptr; // Ownership transferred.

        JS_SetConstructorBit(ctx_, ctorFunc, true);

        // Set prototype.
        JS_SetConstructor(ctx_, ctorFunc, proto_);
        JS_SetClassProto(ctx_, class_id_, JS_DupValue(ctx_, proto_));

        // Set up inheritance if Base is specified.
        if constexpr (!std::is_void_v<Base>) {
            JSClassID base_id = ClassRegistry::classId<Base>();
            if (base_id != 0) {
                JSValue baseProto = JS_GetClassProto(ctx_, base_id);
                if (!JS_IsUndefined(baseProto)) {
                    JS_SetPrototype(ctx_, proto_, baseProto);
                }
                JS_FreeValue(ctx_, baseProto);
            }
        }

        // Install static methods on the constructor.
        for (auto& [smName, jsFns] : static_methods_) {
            if (jsFns.size() == 1) {
                JS_DefinePropertyValueStr(ctx_, ctorFunc, smName.c_str(), jsFns[0],
                                          JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
            } else {
                // Multiple overloads: build overload table for free functions.
                // For now, just use the first one (TODO: proper overload dispatch for statics).
                JS_DefinePropertyValueStr(ctx_, ctorFunc, smName.c_str(), jsFns[0],
                                          JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
                for (size_t i = 1; i < jsFns.size(); ++i) {
                    JS_FreeValue(ctx_, jsFns[i]);
                }
            }
        }
        static_methods_.clear();

        // Install enum values on the constructor.
        for (auto& [evName, jsVal] : enum_values_) {
            JS_DefinePropertyValueStr(ctx_, ctorFunc, evName.c_str(), jsVal,
                                      JS_PROP_ENUMERABLE);
        }
        enum_values_.clear();

        // Install static properties (getter/setter) on the constructor.
        for (auto& sp : static_props_) {
            JSAtom atom = JS_NewAtom(ctx_, sp.name.c_str());
            JS_DefinePropertyGetSet(ctx_, ctorFunc, atom, sp.getter, sp.setter,
                                    JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
            JS_FreeAtom(ctx_, atom);
        }
        static_props_.clear();

        // Free the prototype (ClassBinder's reference; JS_SetClassProto kept a dup).
        JS_FreeValue(ctx_, proto_);
        proto_ = JS_UNDEFINED;

        return ctorFunc;
    }
    JSContext* ctx_;
    JSRuntime* rt_;
    std::string name_;
    JSClassID class_id_ = 0;
    JSValue proto_ = JS_UNDEFINED;
    bool installed_ = false;

    OverloadTable* ctor_table_;
    std::shared_ptr<std::function<void(T*)>> custom_dtor_;

    // Deferred static methods: name -> list of JSValue functions.
    std::vector<std::pair<std::string, std::vector<JSValue>>> static_methods_;

    // Deferred enum values: name -> JSValue.
    std::vector<std::pair<std::string, JSValue>> enum_values_;

    // Deferred static properties: name, getter JSValue, setter JSValue (JS_UNDEFINED if readonly).
    struct StaticPropEntry {
        std::string name;
        JSValue getter;
        JSValue setter;  // JS_UNDEFINED for readonly
    };
    std::vector<StaticPropEntry> static_props_;

    // --- Constructor closure data ---

    struct CtorClosureData {
        OverloadTable* table;
        std::shared_ptr<std::function<void(T*)>> custom_dtor;
        JSClassID class_id;
    };

    static void ctorClosureFinalizer(void* opaque) {
        auto* data = static_cast<CtorClosureData*>(opaque);
        delete data->table;
        delete data;
    }

    /**
     * @brief JSCClosure callback for the JS constructor (`new ClassName(...)`).
     */
    static JSValue constructorCallback(JSContext* ctx, JSValueConst new_target,
                                       int argc, JSValueConst* argv,
                                       int /*magic*/, void* opaque) {
        auto* data = static_cast<CtorClosureData*>(opaque);

        // Dispatch to the matching constructor overload.
        JSValue ptrVal = JS_UNDEFINED;
        bool found = false;
        for (auto& entry : data->table->entries) {
            if (entry.expected_argc == -1 || entry.expected_argc == argc) {
                ptrVal = entry.invoke(ctx, new_target, argc, argv, entry.data);
                found = true;
                break;
            }
        }

        if (!found) {
            return JS_ThrowTypeError(ctx, "no matching constructor overload for %d argument(s)", argc);
        }

        if (JS_IsException(ptrVal)) {
            return ptrVal;
        }

        // Extract the native pointer from the encoded int64.
        int64_t ptrInt = 0;
        JS_ToInt64(ctx, &ptrInt, ptrVal);
        JS_FreeValue(ctx, ptrVal);
        T* nativePtr = reinterpret_cast<T*>(ptrInt);

        if (!nativePtr) {
            return JS_ThrowTypeError(ctx, "constructor returned null");
        }

        // Create the JS object.
        JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
        JSValue jsObj = JS_NewObjectProtoClass(ctx, proto, data->class_id);
        JS_FreeValue(ctx, proto);

        if (JS_IsException(jsObj)) {
            // Clean up the native object.
            if (data->custom_dtor && *data->custom_dtor) {
                (*data->custom_dtor)(nativePtr);
            } else {
                PointerPolicy<T>::destroy(nativePtr);
            }
            return jsObj;
        }

        // Create PointerData.
        PointerData* pd;
        if (data->custom_dtor && *data->custom_dtor) {
            pd = ClassRegistry::makeOwnedCustom<T>(nativePtr,
                [dtor = data->custom_dtor](T* p) { (*dtor)(p); });
        } else {
            pd = ClassRegistry::makeOwned<T>(nativePtr);
        }

        JS_SetOpaque(jsObj, pd);
        return jsObj;
    }

    // --- Finalizer and GC Mark callbacks ---

    static void finalizerCallback(JSRuntime* rt, JSValue val) {
        ClassRegistry::pointerFinalizer(rt, val);
    }

    static void gcMarkCallback(JSRuntime* rt, JSValue val, JS_MarkFunc* mark_func) {
        ClassRegistry::pointerGcMark(rt, val, mark_func);
    }
};

// ============================================================================
// JsConverter specialization for ClassBinder-registered types
// ============================================================================

// NOTE: ClassBinder-registered types need custom JsConverter specializations.
// These are typically added by the user or auto-generated. A generic helper:

/**
 * @brief Helper to create a JsConverter specialization for a ClassBinder-registered type.
 *
 * After binding a class T with ClassBinder, you can use this macro to enable
 * automatic T <-> JSValue conversion:
 *
 * @code
 * QJSBIND_DECLARE_CONVERTER(MyClass);
 * @endcode
 */
#define QJSBIND_DECLARE_CONVERTER(T)                                          \
    template <>                                                                \
    struct JsConverter<T*> {                                                    \
        static JSValue toJs(JSContext* ctx, T* ptr) {                          \
            if (!ptr) return JS_NULL;                                          \
            JSClassID cid = ClassRegistry::classId<T>();                       \
            JSValue proto = JS_GetClassProto(ctx, cid);                        \
            JSValue obj = JS_NewObjectProtoClass(ctx, proto, cid);             \
            JS_FreeValue(ctx, proto);                                          \
            auto* pd = ClassRegistry::makeBorrowed<T>(ptr);                    \
            JS_SetOpaque(obj, pd);                                             \
            return obj;                                                        \
        }                                                                      \
        static T* fromJs(JSContext* ctx, JSValueConst val) {                   \
            JSClassID cid = ClassRegistry::classId<T>();                       \
            auto* pd = static_cast<PointerData*>(JS_GetOpaque2(ctx, val, cid));\
            return pd ? pd->get<T>(ctx) : nullptr;                             \
        }                                                                      \
    };                                                                         \
    template <>                                                                \
    struct JsConverter<T&> {                                                    \
        static JSValue toJs(JSContext* ctx, T& ref) {                          \
            return JsConverter<T*>::toJs(ctx, &ref);                           \
        }                                                                      \
        static T& fromJs(JSContext* ctx, JSValueConst val) {                   \
            T* ptr = JsConverter<T*>::fromJs(ctx, val);                        \
            assert(ptr && "fromJs<T&>: null native object");                   \
            return *ptr;                                                       \
        }                                                                      \
    };                                                                         \
    template <>                                                                \
    struct JsConverter<const T&> {                                              \
        static JSValue toJs(JSContext* ctx, const T& ref) {                    \
            return JsConverter<T*>::toJs(ctx, const_cast<T*>(&ref));           \
        }                                                                      \
        static const T& fromJs(JSContext* ctx, JSValueConst val) {             \
            return JsConverter<T&>::fromJs(ctx, val);                          \
        }                                                                      \
    };                                                                         \
    template <>                                                                \
    struct JsConverter<T> {                                                     \
        static JSValue toJs(JSContext* ctx, const T& val) {                    \
            /* Create an owned copy on the heap so JS can manage its lifetime */\
            T* copy = new T(val);                                              \
            JSClassID cid = ClassRegistry::classId<T>();                       \
            JSValue proto = JS_GetClassProto(ctx, cid);                        \
            JSValue obj = JS_NewObjectProtoClass(ctx, proto, cid);             \
            JS_FreeValue(ctx, proto);                                          \
            auto* pd = ClassRegistry::makeOwned<T>(copy);                      \
            JS_SetOpaque(obj, pd);                                             \
            return obj;                                                        \
        }                                                                      \
        static T fromJs(JSContext* ctx, JSValueConst val) {                    \
            T* ptr = JsConverter<T*>::fromJs(ctx, val);                        \
            assert(ptr && "fromJs<T>: null native object");                    \
            return *ptr;                                                       \
        }                                                                      \
    }

} // namespace qjsbind
