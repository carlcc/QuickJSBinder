# Design

This document explains the design philosophy, key design decisions, and implementation patterns behind QuickJSBinder.

## Design Philosophy

### 1. Zero-Cost Abstraction Where Possible

QuickJSBinder strives to add minimal overhead over raw QuickJS C API calls:

- **Header-only**: No separate compilation unit, enabling full inlining
- **Compile-time dispatch**: `if constexpr`, SFINAE, and template specialization resolve most decisions at compile time
- **No virtual dispatch** in hot paths: `PointerData::destructor` is a plain function pointer, not a virtual method
- **Inline static variables** (C++17): `ClassIdHolder<T>::class_id` avoids separate translation unit definitions

### 2. Ergonomics Over Minimalism

The API is designed to be pleasant to use, even if the implementation is complex:

- **Fluent/chainable builders**: Every registration method returns `*this` for chaining
- **sol2-style proxies**: `ctx["key"] = value` and `ctx["fn"](args...).get<T>()` feel natural to C++ developers familiar with sol2
- **Automatic type deduction**: `JsConverter<T>` eliminates manual `JS_NewInt32` / `JS_ToInt32` calls
- **`JSContext*` auto-injection**: Functions can accept `JSContext*` as a parameter without consuming a JS argument slot

### 3. Safety by Default

- **RAII everywhere**: `JsRuntime`, `JsContext`, `JsValue`, `PointerData` — resources are never leaked if C++ scope is respected
- **Ownership tracking**: `PointerData::owned` flag ensures C++ objects are only destroyed when appropriate
- **Type-safe access**: `PointerData::get<T>()` checks for null and throws a JS TypeError rather than crashing

## Key Design Decisions

### Decision 1: Header-Only Library

**Choice**: All code lives in `.hpp` files under `include/qjsbind/`.

**Rationale**:
- Simplest possible integration — just add an include path
- Template-heavy code naturally lives in headers
- `inline` variables (C++17) eliminate the need for `.cpp` definition files
- The library is not large enough for compilation time to be a concern

**Trade-off**: Every translation unit that includes `qjsbind.hpp` compiles all templates. For very large projects, a precompiled header (PCH) is recommended.

### Decision 2: Traits-Based Type Conversion

**Choice**: `JsConverter<T>` is a traits struct with `toJs()` and `fromJs()` static methods.

**Alternatives considered**:
- Free function overloads (`toJs(ctx, value)`) — harder to extend, ADL issues
- Virtual interface (`Convertible` base class) — too invasive, requires inheritance

**Rationale**:
- Specialization-friendly: users add `JsConverter<MyType>` for custom types
- Works with value types, pointers, references via partial specialization
- SFINAE-compatible: catch-all specializations for arithmetic types and enums
- `QJSBIND_DECLARE_CONVERTER(T)` macro auto-generates 4 specializations (T, T*, T&, const T&)

### Decision 3: Type-Erased Callable Storage

**Choice**: C++ callables are stored via type erasure in `JSCClosure` opaque data.

**Mechanism**:
```cpp
// CallableStorage<F> — heap-allocated, type-erased
struct CallableStorageBase { virtual ~CallableStorageBase() = default; };
template <typename F>
struct CallableStorage : CallableStorageBase {
    F func;
    explicit CallableStorage(F&& f) : func(std::forward<F>(f)) {}
};

// Stored as void* in JSCClosure opaque:
JS_NewCClosure(ctx, callback, name, destructor, 0, 0, new CallableStorage<F>(fn));
```

**Rationale**:
- QuickJS `JSCClosure` provides exactly one `void*` opaque slot
- Type erasure lets us store lambdas of any size (capturing or not)
- The destructor callback ensures proper cleanup when the JS function is GC'd
- No `std::function` overhead for non-capturing lambdas (direct function pointer path)

### Decision 4: Runtime Overload Dispatch by Argument Count

**Choice**: Overloaded C++ functions are dispatched at runtime based on `argc`.

**Mechanism**:
```cpp
struct OverloadEntry {
    JSCFunction* invoke;     // The actual callback
    void* data;              // Opaque data for this overload
    int expected_argc;       // -1 means "accept any"
};

// Dispatch: iterate entries, first match wins
for (auto& entry : table.entries) {
    if (entry.expected_argc == argc || entry.expected_argc == -1) {
        return entry.invoke(ctx, this_val, argc, argv);
    }
}
// No match → JS TypeError
```

**Rationale**:
- C++ overloads typically differ in argument count
- Compile-time full type checking of JS arguments is impossible (JS is dynamically typed)
- Simple, predictable, and fast (linear scan over a small array)
- Edge case: same `argc`, different types → resolved by registration order (first match wins)

**Trade-off**: Does not support overloads that differ only in argument types (e.g., `foo(int)` vs `foo(string)`). This is a deliberate simplification.

### Decision 5: Two-Phase Native Module Registration

**Choice**: `QJSBIND_NATIVE_MODULE` uses a two-pass scheme — dry-run to collect export names, then real execution.

**Why it's needed**: QuickJS requires `JS_AddModuleExport(name)` to be called during `js_init_module` (before the init callback), but `JS_SetModuleExport(name, value)` during the init callback. This means export names must be known before values are created.

**Mechanism**:
```
Pass 1 (dry-run, ctx = nullptr):
  NativeModuleExport records names but doesn't create values
  class_<T>("Vec3", config_lambda)  → records "Vec3", does NOT call lambda
  function("add", fn)               → records "add", does NOT wrap function
  → Collected names → JS_AddModuleExport for each

Pass 2 (real, ctx = valid):
  NativeModuleExport creates values and calls JS_SetModuleExport
  class_<T>("Vec3", config_lambda)  → calls lambda, creates ClassBinder, exports
  function("add", fn)               → wraps function, exports
```

**Rationale**:
- Users write ONE function, the macro handles the rest
- No manual export name arrays to maintain
- Config lambda for `class_<T>` avoids ClassBinder construction with null context

### Decision 6: Lazy Proxy Resolution

**Choice**: `JsProxy` chains do not resolve intermediate values until a terminal operation (`.get()`, `.set()`, `operator()`, etc.).

**Mechanism**:
```cpp
ctx["config"]["debug"].get<bool>()
//   └─ proxy1: root=global, key="config" (not resolved)
//              └─ proxy2: root=proxy1, key="debug" (not resolved)
//                         └─ .get<bool>(): resolve chain → JS_GetPropertyStr × 2
```

**Rationale**:
- Avoids unnecessary `JS_GetPropertyStr` calls for intermediate nodes
- Supports both get and set operations on the same chain
- Matches sol2 semantics that C++ developers expect

### Decision 7: PointerData Ownership Model

**Choice**: `PointerData` tracks ownership with a boolean flag and a type-erased destructor.

**Ownership rules**:
| Creation path | `owned` | Destroyed by |
|---|---|---|
| `new T(args...)` via constructor | `true` | JS GC → `delete p` |
| Factory `constructor2(factory)` | `true` | JS GC → custom dtor or `delete p` |
| C++ passes `T*` (pointer) | `false` | C++ code (not GC) |
| C++ passes `T&` (reference) | `false` | C++ code (not GC) |
| C++ passes `T` (by value) | `true` | JS GC → `delete` heap copy |

**Rationale**:
- Single `PointerData` type handles all ownership scenarios
- `ExtendedPointerData` adds `std::function` dtor only when needed (avoids overhead for common case)
- `PointerPolicy<T>` trait allows customization (e.g., `shared_ptr` support)

## Implementation Patterns

### Pattern 1: SFINAE + `if constexpr` Hybrid

The library uses SFINAE for type trait detection and `if constexpr` for code path selection:

```cpp
// SFINAE: detect if F is a member function pointer
template <typename F>
struct callable_traits;  // primary template — intentionally incomplete

template <typename R, typename C, typename... Args>
struct callable_traits<R(C::*)(Args...)> {  // specialization for member fn
    static constexpr bool is_member_function = true;
    using class_type = C;
    // ...
};

// if constexpr: select code path at compile time
template <typename T, typename F>
JSValue wrapMethod(JSContext* ctx, const char* name, F&& fn, JSClassID class_id) {
    using Traits = callable_traits<std::decay_t<F>>;
    if constexpr (Traits::is_member_function) {
        // Direct member function call path
    } else if constexpr (first_param_is_ref<T, Traits>) {
        // Free function with T& first parameter
    } else {
        // Free function with T* first parameter
    }
}
```

### Pattern 2: Fold Expressions for Argument Conversion

Converting a tuple of C++ types from JS argv uses C++17 fold expressions:

```cpp
template <typename ArgsTuple, std::size_t... Is>
auto convertArgs(JSContext* ctx, JSValueConst* argv, int argc, std::index_sequence<Is...>) {
    using DecayedTuple = /* decay each element */;
    return DecayedTuple{ convertArgAt<ArgsTuple, Is>(ctx, argv, argc)... };
}
```

This generates exactly one `JsConverter<ArgI>::fromJs()` call per argument at compile time.

### Pattern 3: C++17 Inline Static for Per-Type Class IDs

```cpp
template <typename T>
struct ClassIdHolder {
    static inline JSClassID class_id = 0;  // C++17 inline variable
};
```

Each unique C++ type `T` gets its own `JSClassID`, lazily allocated on first use via `JS_NewClassID()`. The `inline` keyword ensures a single definition across all translation units without needing a `.cpp` file.

### Pattern 4: Builder Pattern with Deferred Installation

`ClassBinder` and `JsModule` accumulate registrations and commit them in a single `install()` call:

```
ClassBinder<T>(ctx, "Name")     // allocate class_id, create prototype
    .constructor<...>()          // record ctor overloads
    .method("name", fn)          // define on prototype
    .property("name", g, s)      // define getter/setter on prototype
    .install()                   // build ctor function, set up inheritance,
                                 // install into global/module
```

This allows the builder to set up inheritance and install static members on the constructor function — which requires the constructor to exist — only after all configuration is done.

### Pattern 5: Forward Declaration + Deferred Definition

`ClassBinder::installAsModuleExport(NativeModuleExport&)` needs the full definition of `NativeModuleExport`, but `class_binder.hpp` is included before `js_native_module.hpp`. Solution:

```cpp
// In class_binder.hpp:
void installAsModuleExport(class NativeModuleExport& mod);  // forward-declared parameter

// In js_native_module.hpp (after NativeModuleExport is fully defined):
template <typename T, typename Base>
void ClassBinder<T, Base>::installAsModuleExport(NativeModuleExport& mod) {
    JSValue ctorFunc = buildConstructor();
    mod.exportValue(name_.c_str(), ctorFunc);
}
```

Since `ClassBinder` is a template, its member function definitions can appear anywhere before instantiation. This cleanly breaks the circular dependency.

## Comparison with Alternatives

| Aspect | QuickJSBinder | Raw QuickJS C API |
|---|---|---|
| Type safety | Compile-time checked | Manual casting |
| Resource management | Automatic (RAII) | Manual `JS_FreeValue` |
| Function binding | One-liner | ~20 lines of boilerplate per function |
| Class binding | Fluent builder | ~100 lines of boilerplate per class |
| Overloads | Automatic dispatch | Manual argc checking |
| Type conversion | Automatic | Manual `JS_ToInt32` / `JS_NewString` etc. |
| Learning curve | C++ templates | QuickJS internals |

## Future Directions

- **`std::shared_ptr` support**: Custom `PointerPolicy<T>` specialization for shared ownership
- **Async/Promise integration**: Wrapping C++ coroutines as JS Promises
- **Type-based overload dispatch**: Matching on JS argument types, not just count
- **Compile-time reflection** (C++26): Eliminate the need for manual method/property registration
