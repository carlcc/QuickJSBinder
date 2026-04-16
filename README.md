# QuickJSBinder

**A modern, type-safe C++17 header-only binding library for [QuickJS](https://bellard.org/quickjs/).**

QuickJSBinder provides a fluent, chainable API — inspired by [sol2](https://github.com/ThePhd/sol2) (Lua) — to seamlessly expose C++ classes, functions, and values to JavaScript, and call JS from C++.

```cpp
#include <qjsbind/qjsbind.hpp>
using namespace qjsbind;

int main() {
    JsRuntime rt;
    JsContext ctx(rt);

    // Register a C++ lambda as a global JS function
    ctx["add"] = [](double a, double b) { return a + b; };

    // Bind a C++ class with fluent API
    ClassBinder<Point>(ctx, "Point")
        .constructor<void(), void(double, double)>()
        .method("length", &Point::length)
        .property("x",
            [](const Point& p) { return p.getX(); },
            [](Point& p, double v) { p.setX(v); })
        .install();

    // Call JS from C++
    double result = ctx["add"](10.0, 20.0).get<double>();  // 30.0
}
```

## Features

| Feature | Description |
|---|---|
| **Header-only** | Single `#include <qjsbind/qjsbind.hpp>`, no linking required |
| **C++17** | Leverages `if constexpr`, fold expressions, inline variables, `std::optional` |
| **Fluent API** | Chainable builder pattern for class/module registration |
| **sol2-style proxy** | `ctx["key"] = value`, `ctx["a"]["b"]["c"].get<T>()` |
| **Automatic type conversion** | `bool`, `int`, `double`, `std::string`, `std::vector<T>`, `std::optional<T>`, enums |
| **Function overloading** | Multiple C++ overloads dispatched by argument count |
| **Class inheritance** | `ClassBinder<Derived, Base>` with prototype chain |
| **Factory constructors** | `constructor2(factory_func)` for C API wrappers |
| **Custom destructors** | `destructor(custom_dtor)` for RAII integration |
| **Namespace modules** | `JsModule` with nested sub-modules |
| **ES module support** | `evalFile()` auto-detects `import`/`export` |
| **Native ES modules** | Build `.so`/`.dylib` plugins loadable via `import` |
| **Automatic JSContext injection** | Functions with `JSContext*` parameter get it auto-injected |

## Quick Start

### Requirements

- **C++ compiler** with C++17 support (GCC 8+, Clang 7+, MSVC 19.14+)
- **QuickJS** headers and library
- **xmake** (recommended) or any build system

### Integration

QuickJSBinder is header-only. Just add the `include/` directory to your include path:

```lua
-- xmake.lua
add_includedirs("path/to/QuickJSBinder/include")
```

```cmake
# CMakeLists.txt
target_include_directories(myapp PRIVATE path/to/QuickJSBinder/include)
```

Then in your code:

```cpp
#include <qjsbind/qjsbind.hpp>
using namespace qjsbind;
```

### Building Examples

```bash
xmake f --qjsb_build_examples=y
xmake build
xmake run example_class_binding
```

## Core Concepts

### 1. Runtime & Context (RAII)

```cpp
JsRuntime rt;               // wraps JS_NewRuntime / JS_FreeRuntime
rt.enableModuleLoader();     // enable ES module file loading
JsContext ctx(rt);           // wraps JS_NewContext / JS_FreeContext
```

### 2. sol2-style Proxy

```cpp
// Set global variables and functions
ctx["greeting"] = std::string("Hello");
ctx["add"] = [](double a, double b) { return a + b; };

// Nested objects
ctx["config"]["debug"] = true;
ctx["config"]["version"] = std::string("1.0.0");

// Read values back in C++
double sum = ctx["add"](3.0, 7.0).get<double>();
bool debug = ctx["config"]["debug"].get<bool>();
```

### 3. Class Binding

```cpp
ClassBinder<Point>(ctx, "Point")
    .constructor<void(), void(double, double)>()   // overloaded constructors
    .method("length", &Point::length)              // member function
    .method("add", [](Point& self, double ox, double oy) { ... })  // lambda
    .property("x", getter_lambda, setter_lambda)   // read-write property
    .property_readonly("len", &Point::length)      // read-only property
    .static_method("origin", &Point::origin)       // static method
    .enum_value("Red", Color::Red)                 // enum constant
    .install();                                    // install to global
```

### 4. Modules

```cpp
JsModule math(ctx, "math2");
math.function("add", [](double a, double b) { return a + b; });
math.value("PI", 3.14159265358979);

JsModule trig = math.submodule("trig");
trig.function("sin", static_cast<double(*)(double)>(&std::sin));

math.install();
// JS: math2.add(1, 2);  math2.trig.sin(0.5);
```

### 5. Native ES Modules

Build a `.so`/`.dylib` that JS can `import`:

```cpp
// native_plugin.cpp — compiled as shared library
#include <qjsbind/qjsbind.hpp>
using namespace qjsbind;

static void setupModule(NativeModuleExport& mod) {
    mod.function("add", [](double a, double b) { return a + b; });
    mod.value("VERSION", std::string("1.0.0"));
    mod.class_<Vec3>("Vec3", [](ClassBinder<Vec3>& cls) {
        cls.constructor<void(), void(double, double, double)>()
           .method("length", &Vec3::length);
    });
}

QJSBIND_NATIVE_MODULE(setupModule)
```

```js
// JS side
import { add, VERSION, Vec3 } from "./native_plugin.so";
let v = new Vec3(1, 2, 3);
console.log(v.length());
```

## Documentation

| Document | Description |
|---|---|
| [Tutorial](docs/tutorial.md) | Step-by-step usage guide covering all features |
| [Architecture](docs/architecture.md) | Library architecture and component overview |
| [Design](docs/design.md) | Design philosophy, patterns, and rationale |
| [API Reference](docs/api/) | Doxygen-generated API documentation |

## Project Structure

```
QuickJSBinder/
├── include/qjsbind/
│   ├── qjsbind.hpp            # Unified entry header
│   ├── fwd.hpp                # Forward declarations
│   ├── pointer_data.hpp       # Native object lifetime management
│   ├── js_value.hpp           # JsValue — RAII JSValue wrapper
│   ├── js_runtime.hpp         # JsRuntime — RAII runtime wrapper
│   ├── js_context.hpp         # JsContext — RAII context wrapper
│   ├── js_converter.hpp       # JsConverter<T> — type conversion traits
│   ├── function_wrapper.hpp   # Function wrapping & overload dispatch
│   ├── class_binder.hpp       # ClassBinder<T> — class binding API
│   ├── js_module.hpp          # JsModule — namespace module binding
│   ├── js_native_module.hpp   # NativeModuleExport & QJSBIND_NATIVE_MODULE
│   └── js_proxy.hpp           # JsProxy — sol2-style property proxy
├── examples/
│   ├── common.hpp             # Shared example types (Point, Color, etc.)
│   ├── main.cpp               # Comprehensive demo (all features)
│   ├── example_class_binding.cpp
│   ├── example_proxy.cpp
│   ├── example_module.cpp
│   ├── example_es_module.cpp
│   ├── example_native_module.cpp
│   ├── scripts/               # JS test scripts
│   └── native_scripts/        # Native plugin source + JS tests
├── docs/                      # Documentation
├── xmake.lua                  # Build configuration
└── LICENSE                    # Apache License 2.0
```

## License

[Apache License 2.0](LICENSE)
