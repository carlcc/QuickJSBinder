/**
 * @file fwd.hpp
 * @brief Forward declarations for all core QuickJSBinder types.
 *
 * Include this header when you only need to refer to QuickJSBinder types
 * by pointer or reference (e.g., in function signatures, class members)
 * without pulling in their full definitions.
 *
 * @note Part of the QuickJSBinder header-only C++17 library.
 */
#pragma once

extern "C" {
struct JSContext;
struct JSRuntime;
}

namespace qjsbind {

// RAII wrapper for QuickJS JSValue.
class JsValue;

// RAII wrapper for QuickJS JSRuntime*.
class JsRuntime;

// RAII wrapper for QuickJS JSContext*.
class JsContext;

// Lazy property proxy for intuitive JS object manipulation.
class JsProxy;

// Module/namespace binding facility.
class JsModule;

// Class binding facility.
template <typename T, typename Base>
class ClassBinder;

// Type conversion traits.
template <typename T, typename Enable>
struct JsConverter;

} // namespace qjsbind
