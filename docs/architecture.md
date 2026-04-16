# Architecture

This document describes the architecture of QuickJSBinder — how the library is organized, how its components interact, and the data flows involved.

## High-Level Overview

QuickJSBinder sits between user C++ code and the QuickJS C API, providing a type-safe, ergonomic abstraction layer:

```
┌─────────────────────────────────────────────────┐
│                User C++ Code                    │
│         (application / plugin / game)           │
├─────────────────────────────────────────────────┤
│              QuickJSBinder                      │
│  ┌──────────┐ ┌──────────┐ ┌──────────────────┐│
│  │ JsProxy  │ │ClassBinder│ │ JsModule /       ││
│  │(sol2-like│ │(fluent   │ │ NativeModule-    ││
│  │ proxy)   │ │ builder) │ │ Export           ││
│  └────┬─────┘ └────┬─────┘ └────┬─────────────┘│
│       │            │            │               │
│  ┌────┴────────────┴────────────┴─────────────┐ │
│  │         Core Infrastructure                │ │
│  │  JsValue · JsRuntime · JsContext           │ │
│  │  JsConverter<T> · wrapFunction/wrapMethod  │ │
│  │  PointerData · ClassRegistry               │ │
│  └────────────────────┬───────────────────────┘ │
├───────────────────────┼─────────────────────────┤
│           QuickJS C API (libqjs)                │
│    JS_NewRuntime · JS_NewContext · JS_Eval ...   │
└─────────────────────────────────────────────────┘
```

## Component Dependency Graph

The headers are organized in a strict dependency chain. Each file only depends on those listed before it:

```
fwd.hpp                          (0 deps — forward declarations only)
  └─> pointer_data.hpp           (QuickJS C API)
        └─> js_value.hpp         (pointer_data)
              └─> js_runtime.hpp (js_value)
                    └─> js_context.hpp      (js_runtime, js_value)
                          └─> js_converter.hpp   (js_value, js_context)
                                └─> function_wrapper.hpp  (js_converter, pointer_data)
                                      └─> class_binder.hpp     (function_wrapper, pointer_data)
                                            └─> js_module.hpp        (class_binder, function_wrapper)
                                                  └─> js_native_module.hpp (class_binder, js_module)
                                                        └─> js_proxy.hpp         (js_value, function_wrapper)
```

The unified entry header `qjsbind.hpp` includes all of the above in the correct order.

## Layer Architecture

### Layer 1: Foundation — RAII Wrappers

These components wrap raw QuickJS handles with automatic resource management:

| Component | Wraps | Lifetime Strategy |
|---|---|---|
| `JsRuntime` | `JSRuntime*` | RAII — `JS_NewRuntime()` / `JS_FreeRuntime()` |
| `JsContext` | `JSContext*` | RAII — `JS_NewContext()` / `JS_FreeContext()` |
| `JsValue` | `JSValue` | Reference-counted — `JS_DupValue()` / `JS_FreeValue()` |
| `PointerData` | `void*` (C++ object) | GC-integrated — destroyed on JS finalizer callback |

**Key design**: QuickJS uses a reference-counting GC with cycle detection. `JsValue` mirrors this by calling `JS_DupValue` on copy and `JS_FreeValue` on destruction. `PointerData` bridges C++ object lifetimes into the JS GC cycle.

### Layer 2: Type Conversion — JsConverter

`JsConverter<T>` is a traits-based type conversion system that maps C++ types to/from JS values:

```
C++ Type                         JS Type
─────────────────────────────    ──────────────
bool                         ⟷   Boolean
int32_t / uint32_t           ⟷   Number (int)
int64_t                      ⟷   Number (int64)
double / float               ⟷   Number (float64)
std::string / const char*    ⟷   String
std::vector<T>               ⟷   Array (recursive)
std::optional<T>             ⟷   T | undefined
std::function<R(Args...)>    ←   Function
enum types                   ⟷   Number (underlying type)
JsValue / JSValue            ⟷   Any (passthrough)
JSContext*                   ←   (auto-injected, no JS arg consumed)
ClassBinder-registered T     ⟷   Object with opaque PointerData
```

The system is extensible: users can specialize `JsConverter<T>` for custom types, and `QJSBIND_DECLARE_CONVERTER(T)` auto-generates specializations for class-bound types.

### Layer 3: Function Wrapping & Overload Dispatch

This layer converts arbitrary C++ callables into QuickJS C callbacks (`JSCFunction`):

```
                    C++ callable (lambda / function ptr / member fn ptr)
                                        │
                                        ▼
                              callable_traits<F>
                         (compile-time signature introspection)
                                        │
                         ┌──────────────┼──────────────┐
                         ▼              ▼              ▼
                   Free function   Member function  Lambda/Functor
                         │              │              │
                         ▼              ▼              ▼
                   wrapFunction    wrapMethod     CallableStorage
                         │              │         (type-erased in
                         │              │          CClosure opaque)
                         ▼              ▼              │
                    ┌────┴──────────────┴──────────────┘
                    ▼
            JSCFunction callback
              (registered with QuickJS)
```

**Overload dispatch** is handled at runtime by matching `argc` (number of JS arguments) against registered `expected_argc` for each overload entry. The `OverloadTable` stores multiple entries and the dispatcher tries each in order.

### Layer 4: Class Binding — ClassBinder

`ClassBinder<T, Base>` orchestrates class registration:

```
ClassBinder<T, Base>(ctx, "ClassName")
    │
    ├── Allocates JSClassID (via ClassRegistry)
    ├── Registers JSClassDef (finalizer + gc_mark callbacks)
    ├── Creates prototype object
    │
    │  .constructor<void(Args...)>()
    │   └── Creates OverloadEntry per signature
    │       └── CtorSigParser → new T(Args...) → PointerData
    │
    │  .method("name", &T::method)
    │   └── wrapMethod → OverloadTable → DefineProperty on prototype
    │
    │  .property("name", getter, setter)
    │   └── getter/setter → wrapMethod → JS_DefinePropertyGetSet on prototype
    │
    │  .install()
    │   └── buildConstructor() → JSCClosure
    │       ├── JS_SetConstructorBit
    │       ├── JS_SetConstructor(ctor, proto)
    │       ├── JS_SetClassProto (with dup)
    │       ├── Set up Base prototype chain (if Base ≠ void)
    │       ├── Install static methods on ctor
    │       ├── Install enum values on ctor
    │       └── Define ctor as property on target (global / module / export)
    │
    └── (prototype + class_id kept alive by QuickJS runtime)
```

### Layer 5: Module Systems

#### JsModule (Namespace Modules)

```
JsModule("math2", ctx)
    │
    ├── Creates JS_NewObject as namespace
    ├── .function() → wrapFunction → DefineProperty
    ├── .value() → JsConverter::toJs → DefineProperty
    ├── .submodule("trig") → child JsModule
    │   └── (same registration API)
    │
    └── .install()
        ├── Recursively installs all submodules
        └── Defines namespace object on global (or parent)
```

#### NativeModuleExport (Native ES Modules)

```
QJSBIND_NATIVE_MODULE(setupFunc)
    │
    ├── js_init_module() is auto-generated:
    │
    │   Phase 1: Dry-run (ctx = nullptr)
    │   ├── Call setupFunc(mod) where mod is in dry-run mode
    │   ├── .function("name", ...) → records "name" (no real work)
    │   ├── .value("name", ...) → records "name"
    │   ├── .class_<T>("name", ...) → records "name" (lambda NOT called)
    │   └── Collected names → JS_AddModuleExport for each
    │
    │   Phase 2: Real execution (init callback)
    │   ├── Call setupFunc(mod) where mod has real ctx
    │   ├── .function("name", fn) → wrapFunction → JS_SetModuleExport
    │   ├── .value("name", val) → JsConverter::toJs → JS_SetModuleExport
    │   ├── .class_<T>("name", config) → ClassBinder → installAsModuleExport
    │   └── Returns 0 (success)
    │
    └── QuickJS loads .so via dlopen, calls js_init_module
```

### Layer 6: JsProxy (sol2-style Access)

```
ctx["config"]["debug"]
    │
    ├── ctx.operator[](key)
    │   └── Creates JsProxy(ctx, globalObject, "config")
    │
    ├── proxy.operator[](key)
    │   └── Creates JsProxy(parent_proxy, "debug")
    │       (lazy — does NOT resolve yet)
    │
    ├── .get<bool>()           → resolve() chain → JS_GetPropertyStr → JsConverter<bool>::fromJs
    ├── .set(value)            → resolve parent → JsConverter → JS_SetPropertyStr
    └── .operator()(args...)   → resolve() → JS_Call → return new JsProxy
```

## Object Lifetime Model

```
┌───────────────────────────────────────────────┐
│                 JS Object                     │
│  ┌─────────────────────────────────────────┐  │
│  │  opaque pointer → PointerData           │  │
│  │    ├── ptr: void* → C++ object          │  │
│  │    ├── owned: bool                      │  │
│  │    └── destructor: void(*)(void*)       │  │
│  └─────────────────────────────────────────┘  │
│                                               │
│  When JS GC collects this object:             │
│    → pointerFinalizer() is called             │
│      → PointerData::destroy()                 │
│        → if (owned) destructor(ptr)           │
│      → delete pointerData                     │
└───────────────────────────────────────────────┘

Ownership rules:
  • Objects created by JS `new T(...)` → owned = true
  • Objects passed from C++ as T& / T* → owned = false (borrowed)
  • Objects passed from C++ as T (by value) → owned = true (heap copy)
  • Factory constructors (constructor2) → owned = true
  • Custom destructor overrides default delete
```

## Thread Safety

QuickJSBinder inherits QuickJS's threading model:

- **`JSRuntime`**: Single-threaded. All operations on a runtime and its contexts must happen on the same thread.
- **Multiple runtimes**: Different `JsRuntime` instances can be used on different threads independently.
- **ClassBinder registration**: `JSClassID` allocation uses `JS_NewClassID()` which is atomic, but class registration on a runtime is not thread-safe.

## Error Handling

QuickJSBinder propagates errors through QuickJS's exception mechanism:

1. **Type mismatches** in `JsConverter::fromJs` → `JS_ThrowTypeError`
2. **Null pointer access** in `PointerData::get<T>` → `JS_ThrowTypeError`
3. **Constructor overload miss** → `JS_ThrowTypeError` with expected argc info
4. **JS exceptions** from `ctx.eval()` / `ctx.evalFile()` → returned as `JS_EXCEPTION`; use `JsValue::isException()` or check return codes

The library does **not** use C++ exceptions internally. All errors flow through QuickJS's JS exception system.
