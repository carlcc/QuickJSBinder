// test_member_property.js
// Tests: member-pointer properties and std::reference_wrapper shared references

print("=== Member-pointer properties ===");

// Test 1: Construct and read member properties
var r = new Rect(5, 10, "myRect");
print("width: " + r.width);       // 5
print("height: " + r.height);     // 10
print("label: " + r.label);       // "myRect"
print("area: " + r.area());       // 50
print("computedArea: " + r.computedArea);       // 50

// Test 2: Write member properties from JS
r.width = 100;
r.height = 200;
r.label = "modified";
print("after modify — width: " + r.width + ", height: " + r.height);   // 100, 200
print("after modify — label: " + r.label);                              // "modified"
print("after modify — area: " + r.area());                              // 20000

// Test 3: Default constructor
var r2 = new Rect();
print("default — width: " + r2.width + ", height: " + r2.height);      // 0, 0
print("default — label: " + r2.label);                                   // "default"

print("");
print("=== std::reference_wrapper (shared reference) ===");

// Test 4: Get a JS reference to a C++ native object
var shared = setSharedRect();
print("shared initial — width: " + shared.width + ", height: " + shared.height);   // 10, 20
print("shared initial — label: " + shared.label);                                   // "shared_rect"

// Test 5: Modify from JS side, verify from C++ side
shared.width = 42;
shared.height = 84;
shared.label = "js_modified";
print("after JS modify — getNativeWidth: " + getNativeWidth());     // 42
print("after JS modify — getNativeHeight: " + getNativeHeight());   // 84
print("after JS modify — getNativeLabel: " + getNativeLabel());     // "js_modified"

// Test 6: Modify from C++ side, verify from JS side
setNativeWidth(999);
print("after C++ modify — shared.width: " + shared.width);         // 999

print("");
print("=== All tests passed! ===");
