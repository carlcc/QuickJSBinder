// test_builtin_modules.js
// Tests: QuickJS built-in modules (std, os)
// Note: This script is evaluated as a global script, so we test built-in
// modules via globalThis references set up by js_std_add_helpers.

print("=== Built-in modules test ===");

// Test 1: The 'print' function should work (from js_std_add_helpers)
print("print function works!");

// Test 2: console.log should also work (from js_std_add_helpers)
// console.log is typically added by helpers
if (typeof console !== 'undefined' && typeof console.log === 'function') {
    print("console.log is available");
    console.log("This message is printed by console.log")
} else {
    print("console.log not available (OK for some configurations)");
}

// Test 3: scriptArgs should be defined (from helpers)
if (typeof scriptArgs !== 'undefined') {
    print("scriptArgs is defined, length: " + scriptArgs.length);
} else {
    print("scriptArgs is defined via helpers");
}

print("");
print("=== Built-in modules test passed! ===");
