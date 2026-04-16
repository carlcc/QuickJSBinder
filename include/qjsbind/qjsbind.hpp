/**
 * @file qjsbind.hpp
 * @brief Unified entry header for the QuickJSBinder library.
 *
 * Include this single header to access all QuickJSBinder functionality:
 * - JsValue (RAII JSValue wrapper)
 * - JsRuntime / JsContext (RAII runtime/context wrappers)
 * - JsConverter<T> (type conversion traits)
 * - wrapFunction / wrapMethod (function wrapping)
 * - ClassBinder<T> (class binding)
 * - JsProxy (sol2-like property proxy)
 * - JsModule (module/namespace binding)
 * - PointerData / ClassRegistry (lifetime management)
 * - QJSBIND_DECLARE_CONVERTER macro
 *
 * @note This is a C++17 header-only library. No linking required.
 *
 * @code
 * #include <qjsbind/qjsbind.hpp>
 * using namespace qjsbind;
 * @endcode
 */
#pragma once

// Forward declarations (lightweight, no dependencies).
#include "fwd.hpp"

// Core types (order matters — each file only depends on earlier ones).
#include "pointer_data.hpp"
#include "js_value.hpp"
#include "js_runtime.hpp"
#include "js_context.hpp"
#include "js_converter.hpp"
#include "function_wrapper.hpp"
#include "class_binder.hpp"
#include "js_module.hpp"
#include "js_native_module.hpp"
#include "js_proxy.hpp"
