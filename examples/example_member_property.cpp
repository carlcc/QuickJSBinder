/**
 * @file example_member_property.cpp
 * @brief Demonstrates member-pointer properties and std::reference_wrapper support.
 *
 * Features:
 * - property("name", &T::member) for read-write member access
 * - property_readonly("name", &T::member) for read-only member access
 * - std::reference_wrapper<T> for shared references between JS and C++
 */

#include "common.hpp"

/// A simple 2D rectangle with public members.
struct Rect {
    double width = 0;
    double height = 0;
    std::string label = "default";

    Rect() = default;
    Rect(double w, double h, const std::string& lbl = "rect")
        : width(w), height(h), label(lbl) {}

    double area() const { return width * height; }
};
QJSBIND_DECLARE_CONVERTER(Rect);

int main(int argc, char* argv[]) {
    JsRuntime rt;
    JsContext ctx(rt);
    registerPrint(ctx);

    // --- Bind Rect with member-pointer properties ---
    {
        ClassBinder<Rect> binder(ctx, "Rect");
        binder
            .constructor<void(), void(double, double), void(double, double, std::string)>()
            // Member-pointer based properties (new feature!)
            .property("width", &Rect::width)
            .property("height", &Rect::height)
            .property("label", &Rect::label)
            // Read-only property via member function (callable-based)
            .property_readonly("computedArea", &Rect::area)
            .method("area", &Rect::area)
            .install();
    }

    // --- Test std::reference_wrapper: share a native object with JS ---
    Rect nativeRect(10, 20, "shared_rect");

    {
        // Create a JS function that takes a reference_wrapper
        ctx["setSharedRect"] = [&nativeRect](JSContext* ctx) -> JSValue {
            // Convert the reference_wrapper to JS — creates a borrowed reference
            return JsConverter<std::reference_wrapper<Rect>>::toJs(ctx, std::ref(nativeRect));
        };

        // Getter for verification from C++ side
        ctx["getNativeWidth"] = [&nativeRect]() -> double { return nativeRect.width; };
        ctx["getNativeHeight"] = [&nativeRect]() -> double { return nativeRect.height; };
        ctx["getNativeLabel"] = [&nativeRect]() -> std::string { return nativeRect.label; };

        // Setter from C++ side to verify JS sees the change
        ctx["setNativeWidth"] = [&nativeRect](double w) { nativeRect.width = w; };
    }

    // --- Run the JS test script ---
    std::string scriptDir = argc > 1 ? argv[1] : "examples/scripts";
    return runScriptFile(ctx, scriptDir + "/test_member_property.js");
}
