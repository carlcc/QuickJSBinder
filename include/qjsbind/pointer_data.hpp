/**
 * @file pointer_data.hpp
 * @brief Native object pointer data and lifetime management for QuickJSBinder.
 *
 * This header provides:
 * - PointerData: opaque wrapper stored inside JS objects via JS_SetOpaque,
 *   tracking a native C++ pointer, its ownership semantics (owned vs borrowed),
 *   and destructor dispatch.
 * - ClassRegistry: per-runtime JSClassID allocation, JSClassDef registration,
 *   and PointerData factory methods.
 * - PointerPolicy: extensible traits for custom pointer ownership strategies.
 *
 * @note This is part of the QuickJSBinder header-only C++17 library.
 */
#pragma once

#include <cstddef>
#include <functional>
#include <type_traits>

extern "C" {
#include "quickjs.h"
}

namespace qjsbind {

// ============================================================================
// PointerData
// ============================================================================

/**
 * @brief Opaque data stored in JS objects via JS_SetOpaque.
 *
 * Holds a raw pointer to a native C++ object, ownership flag, a type-erased
 * destructor, and an optional GC-mark callback. This is a POD-like base;
 * the actual instance may be an ExtendedPointerData when a stateful custom
 * destructor is needed.
 */
struct PointerData {
    void* ptr = nullptr;            ///< Raw pointer to the native C++ object.
    bool owned = false;             ///< True = JS owns it; GC will call destructor.

    /// Type-erased destructor: takes the native pointer.
    void (*destructor)(void*) = nullptr;

    /// Optional GC mark callback for native objects that hold JS references.
    void (*gc_marker)(void* ptr, JSRuntime* rt,
                      JS_MarkFunc* mark_func) = nullptr;

    /// Virtual destructor so that ExtendedPointerData subclasses are freed correctly.
    virtual ~PointerData() = default;

    /**
     * @brief Mark the pointer as invalid (null).
     *
     * Called when the native side releases the object. Subsequent JS
     * access via get() will throw a TypeError.
     */
    void invalidate() noexcept { ptr = nullptr; }

    /**
     * @brief Get a typed pointer with null-safety check.
     *
     * @tparam T Expected native C++ type.
     * @param ctx JS context (for throwing TypeError on failure).
     * @return Typed pointer, or nullptr (with pending exception) if released.
     */
    template <class T>
    T* get(JSContext* ctx) const {
        if (!ptr) {
            JS_ThrowTypeError(ctx, "accessing released native object");
            return nullptr;
        }
        return static_cast<T*>(ptr);
    }

    /**
     * @brief Destroy the native object if owned, then invalidate.
     *
     * Subclasses may override to use captured/stateful destructors.
     */
    virtual void destroy() noexcept {
        if (owned && ptr && destructor) {
            destructor(ptr);
            ptr = nullptr;
        }
    }
};

/**
 * @brief Extended PointerData holding a stateful (capturing) destructor.
 *
 * Used when the user provides a capturing lambda or std::function as
 * the custom destructor via ClassBinder::destructor().
 */
struct ExtendedPointerData : PointerData {
    std::function<void(void*)> custom_dtor; ///< Stateful destructor.

    /**
     * @brief Destroy using the captured custom destructor.
     */
    void destroy() noexcept override {
        if (owned && ptr && custom_dtor) {
            custom_dtor(ptr);
            ptr = nullptr;
        }
    }
};

// ============================================================================
// PointerPolicy (extensible traits)
// ============================================================================

/**
 * @brief Extensible pointer ownership policy traits.
 *
 * Default: raw pointer with new/delete. Specialize for shared_ptr, etc.
 *
 * @tparam T The native C++ type.
 */
template <typename T>
struct PointerPolicy {
    using StorageType = T*;

    /** @brief Extract raw pointer from storage. */
    static T* get(StorageType s) noexcept { return s; }

    /** @brief Destroy the stored object via delete. */
    static void destroy(StorageType s) noexcept { delete s; }
};

// ============================================================================
// ClassRegistry
// ============================================================================

namespace detail {

/**
 * @brief Per-type static JSClassID storage (C++17 inline variable).
 * @tparam T The C++ type being registered.
 */
template <typename T>
struct ClassIdHolder {
    static inline JSClassID class_id = 0;
};

} // namespace detail

/**
 * @brief Manages JSClassID allocation and JSClassDef registration for C++ types.
 *
 * Uses compile-time type identity (ClassIdHolder<T>) so that each C++ type
 * gets exactly one JSClassID, lazily allocated on first use.
 */
class ClassRegistry {
public:
    ClassRegistry() = delete;

    /**
     * @brief Get or lazily allocate the JSClassID for type T.
     *
     * @tparam T The C++ type.
     * @param rt JS runtime (needed for first-time allocation).
     * @return The JSClassID associated with T.
     */
    template <typename T>
    static JSClassID classId(JSRuntime* rt) {
        auto& id = detail::ClassIdHolder<T>::class_id;
        if (id == 0) {
            JS_NewClassID(rt, &id);
        }
        return id;
    }

    /**
     * @brief Get the JSClassID for type T (no allocation, may return 0).
     * @tparam T The C++ type.
     */
    template <typename T>
    static JSClassID classId() noexcept {
        return detail::ClassIdHolder<T>::class_id;
    }

    /**
     * @brief Register a JS class for type T if not already registered.
     *
     * Safe to call multiple times — no-op after initial registration.
     *
     * @tparam T The C++ type.
     * @param rt JS runtime.
     * @param def Class definition (name, finalizer, gc_mark, etc.).
     * @return 0 on success, -1 on failure.
     */
    template <typename T>
    static int registerClass(JSRuntime* rt, const JSClassDef* def) {
        JSClassID id = classId<T>(rt);
        if (!JS_IsRegisteredClass(rt, id)) {
            return JS_NewClass(rt, id, def);
        }
        return 0;
    }

    // -----------------------------------------------------------------------
    // PointerData factory helpers
    // -----------------------------------------------------------------------

    /**
     * @brief Create an owned PointerData with the default PointerPolicy<T> destructor.
     *
     * @tparam T Native C++ type.
     * @param ptr Native object pointer (ownership transfers to JS).
     * @return Heap-allocated PointerData (to be passed to JS_SetOpaque).
     */
    template <typename T>
    static PointerData* makeOwned(T* ptr) {
        auto* pd = new PointerData();
        pd->ptr = static_cast<void*>(ptr);
        pd->owned = true;
        pd->destructor = &typedDestructor<T>;
        return pd;
    }

    /**
     * @brief Create an owned PointerData with a custom destructor.
     *
     * Works with function pointers, captureless lambdas, capturing lambdas,
     * and std::function. Stateless callables use a simple function pointer;
     * stateful ones allocate an ExtendedPointerData.
     *
     * @tparam T Native C++ type.
     * @tparam Dtor Callable type taking `T*`.
     * @param ptr Native object pointer.
     * @param dtor The custom destructor.
     * @return Heap-allocated PointerData.
     */
    template <typename T, typename Dtor>
    static PointerData* makeOwnedCustom(T* ptr, Dtor&& dtor) {
        if constexpr (std::is_empty_v<std::decay_t<Dtor>>) {
            // Stateless callable — can be reconstructed from type alone.
            auto* pd = new PointerData();
            pd->ptr = static_cast<void*>(ptr);
            pd->owned = true;
            pd->destructor = [](void* p) {
                std::decay_t<Dtor> d{};
                d(static_cast<T*>(p));
            };
            return pd;
        } else {
            // Stateful callable — store in ExtendedPointerData.
            auto* epd = new ExtendedPointerData();
            epd->ptr = static_cast<void*>(ptr);
            epd->owned = true;
            epd->custom_dtor = [d = std::forward<Dtor>(dtor)](void* p) mutable {
                d(static_cast<T*>(p));
            };
            return epd;
        }
    }

    /**
     * @brief Create a borrowed PointerData (native side retains ownership).
     *
     * JS GC will NOT destroy the native object.
     *
     * @tparam T Native C++ type.
     * @param ptr Native object pointer.
     * @return Heap-allocated PointerData with owned=false.
     */
    template <typename T>
    static PointerData* makeBorrowed(T* ptr) {
        auto* pd = new PointerData();
        pd->ptr = (void*)(ptr);
        pd->owned = false;
        pd->destructor = nullptr;
        return pd;
    }

    // -----------------------------------------------------------------------
    // Standard QuickJS callbacks
    // -----------------------------------------------------------------------

    /**
     * @brief Standard JSClassDef.finalizer for PointerData-based classes.
     *
     * Retrieves PointerData from the JS object, calls destroy(), then frees it.
     *
     * @param rt JS runtime.
     * @param val JS object being finalized.
     */
    static void pointerFinalizer(JSRuntime* rt, JSValue val) {
        (void)rt;
        auto* pd = static_cast<PointerData*>(
            JS_GetOpaque(val, JS_GetClassID(val)));
        if (!pd) return;
        pd->destroy();
        delete pd;
    }

    /**
     * @brief Standard JSClassDef.gc_mark for PointerData-based classes.
     *
     * @param rt JS runtime.
     * @param val JS object being marked.
     * @param mark_func QuickJS mark function.
     */
    static void pointerGcMark(JSRuntime* rt, JSValue val,
                              JS_MarkFunc* mark_func) {
        auto* pd = static_cast<PointerData*>(
            JS_GetOpaque(val, JS_GetClassID(val)));
        if (pd && pd->gc_marker && pd->ptr) {
            pd->gc_marker(pd->ptr, rt, mark_func);
        }
    }

private:
    /** @brief Default typed destructor using PointerPolicy<T>. */
    template <typename T>
    static void typedDestructor(void* p) {
        PointerPolicy<T>::destroy(static_cast<T*>(p));
    }
};

} // namespace qjsbind
