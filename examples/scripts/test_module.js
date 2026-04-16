// =============================================================
// test_module.js
// Tests: JsModule — namespace, nested modules, module value
// =============================================================

print("=== JsModule (math2) Tests ===");
print("math2.add(3, 4) = " + math2.add(3, 4));
print("math2.sub(10, 3) = " + math2.sub(10, 3));
print("math2.mul(6, 7) = " + math2.mul(6, 7));
print("math2.PI = " + math2.PI);
print("math2.E = " + math2.E);
print("math2.trig.sin(0) = " + math2.trig.sin(0));
print("math2.trig.cos(0) = " + math2.trig.cos(0));

print("\n=== JsModule (geom) Tests ===");
if (typeof geom !== "undefined") {
    let gp = new geom.Point(5, 12);
    print("geom.Point(5, 12).length() = " + gp.length());
    print("geom.Point(5, 12).toString() = " + gp.toString());
}

print("\n=== JsModule (Engine) Tests ===");
if (typeof Engine !== "undefined") {
    Engine.log("Hello from JS via Engine.log");
    print("Engine.version = " + Engine.version);
    print("Engine.frameCount = " + Engine.frameCount);
    print("Engine.dynamicProp = " + Engine.dynamicProp);

    // Modify mutable variable
    Engine.frameCount = 100;
    print("Engine.frameCount (after set) = " + Engine.frameCount);
}

print("\n=== JsModule (Graphics) Tests ===");
if (typeof Graphics !== "undefined") {
    print("Graphics.maxFPS = " + Graphics.maxFPS);
    print("Graphics.currentFPS = " + Graphics.currentFPS);

    // Verify property set via JsModule::operator[] from C++
    print("Graphics.renderMode = " + Graphics.renderMode);

    // Call function registered in module
    Graphics.draw("triangle from JS");
}

print("\n=== Module Done ===");
