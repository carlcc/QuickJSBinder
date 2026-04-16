# Tutorial

This tutorial walks through every major feature of QuickJSBinder with practical, runnable examples.

## Table of Contents

- [Getting Started](#getting-started)
- [Runtime & Context](#runtime--context)
- [sol2-style Proxy (JsProxy)](#sol2-style-proxy-jsproxy)
- [Class Binding (ClassBinder)](#class-binding-classbinder)
- [Modules (JsModule)](#modules-jsmodule)
- [ES Modules](#es-modules)
- [Native ES Modules (.so/.dylib)](#native-es-modules-sodylib)
- [Type Conversion Reference](#type-conversion-reference)
- [Advanced Topics](#advanced-topics)

---

## Getting Started

### Installation

QuickJSBinder is header-only. Add the `include/` directory to your project:

```lua
-- xmake.lua
target("myapp", function()
    set_kind("binary")
    set_languages("c++17")  -- or c++20
    add_files("src/*.cpp")
    add_includedirs("path/to/QuickJSBinder/include")
    add_includedirs("path/to/quickjs/include")
    add_links("qjs")
end)
```

### Minimal Example

```cpp
#include <qjsbind/qjsbind.hpp>
#include <iostream>

using namespace qjsbind;

int main() {
    JsRuntime rt;
    JsContext ctx(rt);

    // Register a print function
    ctx["print"] = [](std::string msg) { std::cout << msg << "\n"; };

    // Run JavaScript
    ctx.eval(R"(
        print("Hello from JavaScript!");
    )");

    return 0;
}
```

---

## Runtime & Context

`JsRuntime` and `JsContext` are RAII wrappers around QuickJS's `JSRuntime*` and `JSContext*`.

```cpp
JsRuntime rt;                    // creates JS runtime
JsContext ctx(rt);               // creates JS context within runtime

// Access raw pointers when needed
JSRuntime* rawRt = rt.get();     // or implicit: (JSRuntime*)rt
JSContext* rawCtx = ctx.get();   // or implicit: (JSContext*)ctx
```

### Evaluating Code

```cpp
// Evaluate a string as script
JsValue result = ctx.eval("1 + 2", "<eval>");
if (!result.isException()) {
    std::cout << result.toDouble() << "\n";  // 3.0
}

// Evaluate a file (auto-detects ES modules via import/export keywords)
JsValue result2 = ctx.evalFile("path/to/script.js");

// Evaluate a string explicitly as an ES module
JsValue result3 = ctx.evalModule(R"(
    import { add } from './math.js';
    console.log(add(1, 2));
)", "main.js");
```

### Enabling Module Loading

To load ES modules from files, enable the built-in module loader:

```cpp
JsRuntime rt;
rt.enableModuleLoader();  // enables import from file system
JsContext ctx(rt);

ctx.evalFile("app.js");  // can now use import/export
```

---

## sol2-style Proxy (JsProxy)

JsProxy provides an intuitive, sol2-like syntax for interacting with JS from C++.

### Setting Global Variables

```cpp
ctx["greeting"] = std::string("Hello, World!");
ctx["magicNumber"] = 42;
ctx["pi"] = 3.14159;
ctx["flag"] = true;
```

### Registering Functions

```cpp
// Lambda
ctx["add"] = [](double a, double b) { return a + b; };

// With std::string parameters (automatic conversion)
ctx["greet"] = [](std::string name) -> std::string {
    return "Hello, " + name + "!";
};

// Functions with JSContext* parameter (auto-injected, doesn't consume a JS arg)
ctx["createObject"] = [](JSContext* ctx) -> JSValue {
    return JS_NewObject(ctx);
};
```

### Creating Nested Objects

```cpp
// Create a config namespace
ctx["config"] = JsValue::adopt(ctx, JS_NewObject(ctx));
ctx["config"]["debug"] = true;
ctx["config"]["version"] = std::string("1.0.0");
ctx["config"]["maxRetries"] = 3;
```

### Reading Values in C++

```cpp
std::string greeting = ctx["greeting"].get<std::string>();
int magic = ctx["magicNumber"].get<int>();
bool debug = ctx["config"]["debug"].get<bool>();
```

### Calling JS Functions from C++

```cpp
// Register and call
ctx["add"] = [](double a, double b) { return a + b; };
double sum = ctx["add"](10.0, 20.0).get<double>();  // 30.0

// Call with multiple types
ctx["greet"] = [](std::string name) -> std::string {
    return "Hello, " + name + "!";
};
std::string msg = ctx["greet"](std::string("World")).get<std::string>();
```

### Proxy on Any JsValue

```cpp
JsValue global = ctx.globalObject();
double sum = global["add"](3.0, 7.0).get<double>();

JsValue config = global.getProperty("config");
bool debug = config["debug"].get<bool>();
config["newFlag"] = true;  // set via JsValue proxy
```

> **Full example**: [examples/example_proxy.cpp](../examples/example_proxy.cpp)

---

## Class Binding (ClassBinder)

`ClassBinder<T, Base>` provides a fluent API to expose C++ classes to JavaScript.

### Basic Class

```cpp
class Point {
public:
    Point() : x_(0), y_(0) {}
    Point(double x, double y) : x_(x), y_(y) {}
    double getX() const { return x_; }
    double getY() const { return y_; }
    void setX(double x) { x_ = x; }
    void setY(double y) { y_ = y; }
    double length() const { return std::sqrt(x_*x_ + y_*y_); }
    std::string toString() const {
        return "Point(" + std::to_string(x_) + ", " + std::to_string(y_) + ")";
    }
private:
    double x_, y_;
};
```

### Constructor Overloading

Register multiple constructors with different signatures:

```cpp
ClassBinder<Point>(ctx, "Point")
    .constructor<void(), void(double, double)>()
    // void()           → new Point()
    // void(double, double) → new Point(x, y)
    .install();
```

In JS:
```js
let p1 = new Point();        // default constructor
let p2 = new Point(3, 4);    // parameterized constructor
```

### Methods

Bind member functions, lambdas, or free functions as methods:

```cpp
ClassBinder<Point>(ctx, "Point")
    .constructor<void(), void(double, double)>()

    // Member function pointer
    .method("getX", &Point::getX)
    .method("length", &Point::length)

    // Lambda (first parameter is T& → treated as this)
    .method("add", [](Point& self, double ox, double oy) -> std::string {
        Point result(self.getX() + ox, self.getY() + oy);
        return result.toString();
    })

    // Free function (first parameter is T& → treated as this)
    .method("distFromOrigin", &point_distance_from_origin)
    // where: double point_distance_from_origin(Point& p) { return p.length(); }

    .install();
```

### Method Overloading

Pass multiple callables to `.method()` for automatic overload dispatch:

```cpp
.method("addScalar",
    [](Point& self, double scalar) { ... },       // 1 arg
    [](Point& self, double dx, double dy) { ... }) // 2 args
```

### Properties

```cpp
ClassBinder<Point>(ctx, "Point")
    .constructor<void(double, double)>()

    // Read-write property (getter + setter lambdas)
    .property("x",
        [](const Point& p) -> double { return p.getX(); },
        [](Point& p, double v) { p.setX(v); })

    // Read-only property (member function as getter)
    .property_readonly("len", &Point::length)

    .install();
```

In JS:
```js
let p = new Point(3, 4);
p.x = 10;           // setter
console.log(p.x);   // getter → 10
console.log(p.len);  // read-only → 10.77...
```

### Static Methods

Static methods are installed on the constructor function itself:

```cpp
.static_method("origin", [](JSContext* ctx) -> JSValue {
    // Create a Point(0, 0) and return it as a JS object
    Point* p = new Point(0, 0);
    JSClassID cid = ClassRegistry::classId<Point>();
    JSValue proto = JS_GetClassProto(ctx, cid);
    JSValue obj = JS_NewObjectProtoClass(ctx, proto, cid);
    JS_FreeValue(ctx, proto);
    JS_SetOpaque(obj, ClassRegistry::makeOwned<Point>(p));
    return obj;
})
```

In JS:
```js
let origin = Point.origin();
```

### Enum Values

Enum constants are installed on the constructor:

```cpp
enum class Color { Red = 0, Green = 1, Blue = 2 };

ClassBinder<ColorPoint, Point>(ctx, "ColorPoint")
    .constructor<void(double, double, Color)>()
    .method("getColor", &ColorPoint::getColor)
    .enum_value("Red", Color::Red)
    .enum_value("Green", Color::Green)
    .enum_value("Blue", Color::Blue)
    .install();
```

In JS:
```js
let cp = new ColorPoint(1, 2, ColorPoint.Green);
console.log(cp.getColor());  // 1
```

### Inheritance

Use the second template parameter to specify the base class:

```cpp
// First, register the base class
ClassBinder<Point>(ctx, "Point")
    .constructor<void(), void(double, double)>()
    .method("length", &Point::length)
    .install();

// Then register the derived class with Base = Point
ClassBinder<ColorPoint, Point>(ctx, "ColorPoint")
    .constructor<void(double, double, Color)>()
    .method("getColor", &ColorPoint::getColor)
    .install();
```

In JS:
```js
let cp = new ColorPoint(3, 4, ColorPoint.Red);
console.log(cp.length());  // 5.0 — inherited from Point
```

### Factory Constructors

For types that are created via factory functions (e.g., C API handles):

```cpp
// C API:
// CHandle* c_api_create(int id);
// void c_api_destroy(CHandle* h);

ClassBinder<CHandle>(ctx, "CHandle")
    .constructor2(&c_api_create)        // factory constructor
    .destructor(&c_api_destroy)         // custom destructor
    .method("getData", &c_api_get_data)
    .property("data",
        [](CHandle* self) -> std::string { return self->data; },
        [](CHandle* self, const std::string& d) { self->data = d; })
    .install();
```

In JS:
```js
let h = new CHandle(42);     // calls c_api_create(42)
h.data = "hello";            // calls c_api_set_data
// When h is garbage collected → c_api_destroy is called
```

### Installing to Different Targets

```cpp
// Install to global object (default)
binder.install();

// Install into a JsModule namespace
JsModule geom(ctx, "geom");
binder.installInto(geom.object());
geom.install();

// Install as a native ES module export
binder.installAsModuleExport(mod);
```

### QJSBIND_DECLARE_CONVERTER Macro

After binding a class, use this macro to enable automatic conversion between C++ and JS:

```cpp
QJSBIND_DECLARE_CONVERTER(Point);

// Now Point can be used as function parameters and return values:
ctx["distance"] = [](const Point& a, const Point& b) -> double {
    double dx = a.getX() - b.getX();
    double dy = a.getY() - b.getY();
    return std::sqrt(dx*dx + dy*dy);
};
```

> **Full example**: [examples/example_class_binding.cpp](../examples/example_class_binding.cpp)

---

## Modules (JsModule)

`JsModule` creates namespace-like objects on the global scope.

### Basic Module

```cpp
JsModule math(ctx, "math2");
math.function("add", [](double a, double b) { return a + b; });
math.function("sub", [](double a, double b) { return a - b; });
math.value("PI", 3.14159265358979);
math.value("E",  2.71828182845905);
math.install();
```

In JS:
```js
let sum = math2.add(1, 2);     // 3
console.log(math2.PI);         // 3.14159...
```

### Nested Sub-modules

```cpp
JsModule math(ctx, "math2");
math.function("add", [](double a, double b) { return a + b; });

JsModule trig = math.submodule("trig");
trig.function("sin", static_cast<double(*)(double)>(&std::sin));
trig.function("cos", static_cast<double(*)(double)>(&std::cos));

math.install();  // installs math2 and math2.trig recursively
```

In JS:
```js
let y = math2.trig.sin(0.5);
```

### Installing Classes into Modules

```cpp
JsModule geom(ctx, "geom");

ClassBinder<Point>(ctx, "Point")
    .constructor<void(double, double)>()
    .method("length", &Point::length)
    .installInto(geom.object());  // install Point into geom, not global

geom.install();
```

In JS:
```js
let p = new geom.Point(3, 4);
console.log(p.length());  // 5
```

### Mutable Variables

```cpp
JsModule engine(ctx, "Engine");
engine.variable("speed", 100);  // writable (unlike .value() which is read-only)
engine.install();
```

In JS:
```js
console.log(Engine.speed);  // 100
Engine.speed = 200;          // OK — writable
```

### Dynamic Access

```cpp
// After install, retrieve the module
JsValue mathVal = ctx.getModule("math2");            // as JsValue
JsProxy mathProxy = ctx.module("math2");             // as JsProxy

// Call functions from C++
double sum = ctx.module("math2")["add"](1.0, 2.0).get<double>();

// sol2-style operator[] on module
JsModule mod(ctx, "mymod");
mod["key"] = std::string("value");  // set directly
mod.install();
```

> **Full example**: [examples/example_module.cpp](../examples/example_module.cpp)

---

## ES Modules

QuickJSBinder supports standard ES modules (`import`/`export`).

### Setup

```cpp
JsRuntime rt;
rt.enableModuleLoader();   // REQUIRED for file-based imports
JsContext ctx(rt);
```

### Loading ES Module Files

```cpp
// evalFile auto-detects ES modules (by scanning for import/export keywords)
JsValue result = ctx.evalFile("app.js");
```

`app.js`:
```js
import { add, PI } from './math_utils.js';
import Vec2 from './vec2.js';

let sum = add(1, 2);
let v = new Vec2(3, 4);
console.log(v.length());
```

### Mixing C++ and ES Modules

Register C++ functions before loading ES modules:

```cpp
JsRuntime rt;
rt.enableModuleLoader();
JsContext ctx(rt);

// Register C++ functions as globals
ctx["print"] = [](std::string msg) { std::cout << msg << "\n"; };
ctx["cppAdd"] = [](double a, double b) { return a + b; };

// Load ES module — it can call cppAdd and print
ctx.evalFile("examples/scripts/test_es_module.js");
```

### Inline ES Modules

```cpp
ctx.evalModule(R"(
    export function hello() {
        return "world";
    }
)", "inline_module.js");
```

> **Full example**: [examples/example_es_module.cpp](../examples/example_es_module.cpp)

---

## Native ES Modules (.so/.dylib)

Build C++ code as a shared library that JavaScript can `import` directly.

### Writing a Native Module

```cpp
// native_plugin.cpp
#include <qjsbind/qjsbind.hpp>
using namespace qjsbind;

class Vec3 {
public:
    Vec3() : x_(0), y_(0), z_(0) {}
    Vec3(double x, double y, double z) : x_(x), y_(y), z_(z) {}
    double length() const { return std::sqrt(x_*x_ + y_*y_ + z_*z_); }
    // ... more methods
private:
    double x_, y_, z_;
};

QJSBIND_DECLARE_CONVERTER(Vec3);

static void setupModule(NativeModuleExport& mod) {
    // Export functions
    mod.function("add", [](double a, double b) { return a + b; });
    mod.function("greet", [](std::string name) -> std::string {
        return "Hello, " + name + "!";
    });

    // Export constants
    mod.value("VERSION", std::string("1.0.0"));
    mod.value("GOLDEN_RATIO", 1.618033988749895);

    // Export a class
    mod.class_<Vec3>("Vec3", [](ClassBinder<Vec3>& cls) {
        cls.constructor<void(), void(double, double, double)>()
           .method("length", &Vec3::length)
           .property("x",
               [](const Vec3& v) { return v.getX(); },
               [](Vec3& v, double val) { v.setX(val); });
    });
}

// This macro auto-generates the js_init_module entry point
QJSBIND_NATIVE_MODULE(setupModule)
```

### Building

```lua
-- xmake.lua
target("native_plugin", function()
    set_kind("shared")
    set_languages("c++17")
    add_files("native_plugin.cpp")
    add_includedirs("path/to/QuickJSBinder/include")
    add_includedirs("path/to/quickjs/include")
    add_links("qjs")
end)
```

### Using from JavaScript

```js
import { add, greet, VERSION, GOLDEN_RATIO, Vec3 } from "./native_plugin.so";

console.log(add(10, 20));         // 30
console.log(greet("World"));     // "Hello, World!"
console.log(VERSION);            // "1.0.0"

let v = new Vec3(1, 2, 3);
console.log(v.length());         // 3.741...
v.x = 10;
```

### How It Works

The `QJSBIND_NATIVE_MODULE(setupFunc)` macro generates a `js_init_module` entry point that:

1. **Phase 1 (dry-run)**: Calls `setupFunc` with a dummy `NativeModuleExport` (no context) to collect all export names
2. **Phase 2 (real)**: Uses the collected names for `JS_AddModuleExport`, then calls `setupFunc` again with a real context to register the actual values via `JS_SetModuleExport`

This means you never need to manually maintain an export name list.

> **Full example**: [examples/native_scripts/native_plugin.cpp](../examples/native_scripts/native_plugin.cpp)

---

## Type Conversion Reference

QuickJSBinder automatically converts between C++ and JS types via `JsConverter<T>`:

| C++ Type | JS Type | Direction |
|---|---|---|
| `bool` | Boolean | ⟷ |
| `int32_t`, `uint32_t` | Number (integer) | ⟷ |
| `int64_t` | Number (int64) | ⟷ |
| `uint64_t` | BigInt | ⟷ |
| `float`, `double` | Number (float64) | ⟷ |
| `std::string` | String | ⟷ |
| `const char*` | String / null | → JS only |
| `std::vector<T>` | Array | ⟷ (recursive) |
| `std::optional<T>` | T \| undefined | ⟷ |
| `std::function<R(Args...)>` | Function | ← JS only |
| `JsValue` / `JSValue` | Any | ⟷ (passthrough) |
| `JSContext*` | *(auto-injected)* | ← (no JS arg consumed) |
| Enum types | Number (underlying) | ⟷ |
| ClassBinder-registered `T` | Object | ⟷ (via `QJSBIND_DECLARE_CONVERTER`) |

### Custom Type Conversion

Specialize `JsConverter<T>` for your own types:

```cpp
namespace qjsbind {
template <>
struct JsConverter<MyPoint> {
    static JSValue toJs(JSContext* ctx, const MyPoint& p) {
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, p.x));
        JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, p.y));
        return obj;
    }

    static MyPoint fromJs(JSContext* ctx, JSValueConst val) {
        MyPoint p;
        JSValue xv = JS_GetPropertyStr(ctx, val, "x");
        JSValue yv = JS_GetPropertyStr(ctx, val, "y");
        JS_ToFloat64(ctx, &p.x, xv);
        JS_ToFloat64(ctx, &p.y, yv);
        JS_FreeValue(ctx, xv);
        JS_FreeValue(ctx, yv);
        return p;
    }
};
} // namespace qjsbind
```

### JSContext* Auto-Injection

If a C++ function takes `JSContext*` as a parameter, it is automatically injected without consuming a JS argument:

```cpp
ctx["createArray"] = [](JSContext* ctx, int size) -> JSValue {
    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < size; ++i) {
        JS_SetPropertyUint32(ctx, arr, i, JS_NewInt32(ctx, i));
    }
    return arr;
};
```

In JS:
```js
let arr = createArray(5);  // only 1 arg in JS, ctx is auto-injected
```

---

## Advanced Topics

### JsValue — RAII JSValue Wrapper

`JsValue` manages `JSValue` reference counting automatically:

```cpp
// Adopt an owned JSValue (no dup)
JsValue val = JsValue::adopt(ctx, JS_NewInt32(ctx, 42));

// Dup a borrowed JSValue
JsValue val2 = JsValue::dup(ctx, some_borrowed_value);

// Type checking
if (val.isNumber()) {
    double d = val.toDouble();
}
if (val.isString()) {
    std::string s = val.toString();
}

// Property access
JsValue obj = JsValue::adopt(ctx, JS_NewObject(ctx));
obj.setProperty("key", JsValue::adopt(ctx, JS_NewString(ctx, "value")));
JsValue prop = obj.getProperty("key");

// Release ownership (for passing to QuickJS API that takes ownership)
JSValue raw = val.release();
```

### Object Lifetime & Ownership

When binding classes, ownership is determined by how objects are passed:

```cpp
// Owned by JS (GC will delete):
ClassBinder<T>(ctx, "T")
    .constructor<void(int)>()  // new T(42) → owned
    .install();

// Borrowed by JS (C++ owns the object):
T* myObj = new T(42);
// Pass as T* or T& in JsConverter → borrowed
// JS can use it, but C++ is responsible for deletion

// Custom destructor:
ClassBinder<CHandle>(ctx, "CHandle")
    .constructor2(&c_api_create)
    .destructor(&c_api_destroy)  // called instead of delete
    .install();
```

### Overload Resolution

Overloads are resolved by argument count at runtime:

```cpp
ClassBinder<Point>(ctx, "Point")
    .constructor<
        void(),                  // 0 args
        void(double, double)     // 2 args
    >()
    .method("add",
        &Point::addPoint,        // 1 arg (Point)
        &Point::addScalar)       // 1 arg (double) — same count!
    .install();
```

> **Note**: If two overloads have the same argument count, the first registered one is selected. Avoid this situation by using different method names or different argument counts.

### Error Handling

```cpp
JsValue result = ctx.eval("invalid syntax {{{{");
if (result.isException()) {
    // Get the exception object
    JsValue exc = JsValue::adopt(ctx, JS_GetException(ctx));
    if (exc.isError()) {
        std::string msg = exc.getProperty("message").toString();
        std::string stack = exc.getProperty("stack").toString();
        std::cerr << "Error: " << msg << "\n" << stack << "\n";
    }
}
```
