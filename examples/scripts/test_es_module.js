/**
 * @file test_es_module.js
 * @brief Main ES module that imports other modules.
 *
 * Demonstrates:
 * - Named imports from math_utils.js
 * - Default import of Vec2 class from vec2.js
 * - Using C++-registered global functions (print, greet) from within modules
 */

import { add, mul, PI, clamp, distance } from "./math_utils.js";
import Vec2 from "./vec2.js";

print("=== ES Module Tests ===");

// --- Test named imports from math_utils ---
print("\n--- math_utils imports ---");
print("add(10, 20) = " + add(10, 20));
print("mul(6, 7)   = " + mul(6, 7));
print("PI          = " + PI);
print("clamp(15, 0, 10) = " + clamp(15, 0, 10));
print("clamp(-5, 0, 10) = " + clamp(-5, 0, 10));
print("clamp(5, 0, 10)  = " + clamp(5, 0, 10));
print("distance(0,0, 3,4) = " + distance(0, 0, 3, 4));

// --- Test default import of Vec2 class ---
print("\n--- Vec2 class import ---");
const a = new Vec2(3, 4);
const b = new Vec2(1, 2);
print("a = " + a.toString());
print("b = " + b.toString());
print("a.length() = " + a.length());

const c = a.add(b);
print("a + b = " + c.toString());

const d = a.scale(2);
print("a * 2 = " + d.toString());

// --- Use C++-registered global function ---
print("\n--- C++ interop from module ---");
if (typeof greet === "function") {
    print("greet('ES Module') = " + greet("ES Module"));
}
if (typeof nativeAdd === "function") {
    print("nativeAdd(100, 200) = " + nativeAdd(100, 200));
}

print("\n=== ES Module Done ===");
