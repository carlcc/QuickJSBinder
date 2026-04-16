// =============================================================
// test_class_binding.js
// Tests: ClassBinder — constructor, method, property, static,
//        enum, inheritance, factory constructor
// =============================================================

print("=== Point Tests ===");
let p1 = new Point(3, 4);
print("p1 = " + p1.toString());
print("p1.length() = " + p1.length());
print("p1.x = " + p1.x);
print("p1.y = " + p1.y);
print("p1.len = " + p1.len);

p1.x = 10;
print("After p1.x = 10: " + p1.toString());

print("p1.distFromOrigin() = " + p1.distFromOrigin());
print("p1.add(1, 2) = " + p1.add(1, 2));

let p0 = new Point();
print("p0 = " + p0.toString());

print("\n=== CHandle Tests ===");
let h = new CHandle(42);
print("h.id = " + h.id);
print("h.data = " + h.data);
h.data = "modified";
print("h.getData() = " + h.getData());

print("\n=== ColorPoint Tests ===");
let cp = new ColorPoint(1, 2, ColorPoint.Green);
print("cp.toString() = " + cp.toString());
print("cp.getColor() = " + cp.getColor());
print("cp.length() = " + cp.length());

print("\n=== Class Binding Done ===");
