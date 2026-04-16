// =============================================================
// test_proxy.js
// Tests: JsProxy — property access, assignment, function calls
// =============================================================

print("=== JsProxy Tests ===");

// Functions registered via ctx["key"] = lambda
print("add(3, 4) = " + add(3, 4));
print("mul(6, 7) = " + mul(6, 7));

// Values registered via proxy
print("greeting = " + greeting);
print("magicNumber = " + magicNumber);

// Nested config object
print("config.debug = " + config.debug);
print("config.version = " + config.version);
print("config.maxRetries = " + config.maxRetries);

print("\n=== Proxy Done ===");
