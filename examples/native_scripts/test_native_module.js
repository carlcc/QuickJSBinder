/**
 * Test: loading a native shared library (.so/.dylib) as an ES module.
 *
 * The native plugin exports:
 *   - add(a, b), mul(a, b), greet(name)  — functions
 *   - VERSION, GOLDEN_RATIO               — constants
 *   - Vec3                                 — class
 */

// Import from the native shared library.
// QuickJS resolves this via the module loader; the .so/.dylib
// must be in the same directory or resolvable path.
import { add, mul, greet, VERSION, GOLDEN_RATIO, Vec3 } from "./native_plugin.so";

print("=== Native Module Tests ===");
print("");

// -- Test functions --
print("add(10, 20)     = " + add(10, 20));
print("mul(6, 7)       = " + mul(6, 7));
print("greet('World')  = " + greet("World"));

// -- Test constants --
print("VERSION         = " + VERSION);
print("GOLDEN_RATIO    = " + GOLDEN_RATIO);

// -- Test Vec3 class --
print("");
print("--- Vec3 Tests ---");

const a = new Vec3(1, 2, 3);
print("a = " + a.toString());
print("a.length() = " + a.length());
print("a.x = " + a.x + ", a.y = " + a.y + ", a.z = " + a.z);

const b = new Vec3(4, 5, 6);
print("b = " + b.toString());

// Dot product
print("a.dot(b) = " + a.dot(b));  // 1*4 + 2*5 + 3*6 = 32

// Cross product
const c = a.cross(b);
print("a.cross(b) = " + c.toString());  // (-3, 6, -3)

// Normalize
const n = a.normalize();
print("a.normalize() = " + n.toString());
print("a.normalize().length() ≈ " + n.length());  // ≈ 1.0

// Modify properties
a.x = 100;
print("After a.x = 100: a = " + a.toString());

print("");
print("=== Native Module Tests Done ===");
