/**
 * @file stl_container_wrappers.hpp
 * @brief Type-erased JS-accessible wrappers for STL containers.
 *
 * Provides concrete, type-erased wrapper classes for template containers that
 * cannot be directly registered as single JS classes:
 *
 * - VectorWrapper  — std::vector<T>
 * - MapWrapper     — std::map<K,V> / std::unordered_map<K,V>
 * - SetWrapper     — std::set<K> / std::unordered_set<K>
 *
 * Each wrapper holds a void* to the actual container and a set of function
 * pointers for type-aware operations.  Modifications through the wrapper are
 * reflected in the underlying C++ container (borrowed / reference semantics).
 *
 * Auto-registration: wrapper types are lazily registered on first use
 * (once per JSContext).  No manual bind/install call is needed — simply
 * pass a container to JS and it will be wrapped automatically.
 *
 * Usage:
 * @code
 *   // Just pass a container to JS — auto-registration happens on first use:
 *   std::vector<int> v = {1, 2, 3};
 *   // In a bound method returning std::vector<int>&:
 *   //   JS receives a VectorWrapper with push_back, get, size, etc.
 *
 *   // Manual wrapper creation (also auto-registers):
 *   auto* w = qjsbind::makeVectorWrapper(ctx, &v);
 * @endcode
 *
 * @note Part of the QuickJSBinder header-only C++17 library.
 */
#pragma once

#include "../class_binder.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace qjsbind {

// ============================================================================
// VectorWrapper
// ============================================================================

class VectorWrapper {
public:
    using GetElement    = JsValue (*)(JSContext* ctx, void* stl_vec, size_t index);
    using SetElement    = void    (*)(JSContext* ctx, void* stl_vec, size_t index, JsValue value);
    using PushBack      = void    (*)(JSContext* ctx, void* stl_vec, JsValue element);
    using PopBack       = void    (*)(void* stl_vec);
    using Insert        = void    (*)(JSContext* ctx, void* stl_vec, size_t index, JsValue value);
    using Erase         = void    (*)(void* stl_vec, size_t index);
    using Clear         = void    (*)(void* stl_vec);
    using Size          = size_t  (*)(void* stl_vec);
    using Empty         = bool    (*)(void* stl_vec);
    using Reserve       = void    (*)(void* stl_vec, size_t cap);
    using ShrinkToFit   = void    (*)(void* stl_vec);
    using ToJSArray     = JsValue (*)(JSContext* ctx, void* stl_vec);
    using ForEach       = void    (*)(JSContext* ctx, void* stl_vec, JsValue callback);

    static constexpr const char* className() { return "VectorWrapper"; }

    static void bind(ClassBinder<VectorWrapper>& cls)
    {
        cls.method("get", [](VectorWrapper& vec, int32_t index) -> JsValue {
            return vec.getAt(index);
        });
        cls.method("set", [](VectorWrapper& vec, int32_t index, JsValue value) {
            vec.setAt(index, value);
        });
        cls.method("front", [](VectorWrapper& vec) -> JsValue {
            return vec._get(vec._ctx, vec._stl_vec, 0);
        });
        cls.method("back", [](VectorWrapper& vec) -> JsValue {
            size_t sz = vec._size(vec._stl_vec);
            if (sz == 0) return JsValue(vec._ctx);
            return vec._get(vec._ctx, vec._stl_vec, sz - 1);
        });
        cls.method("push_back", [](VectorWrapper& vec, JsValue element) {
            vec._push_back(vec._ctx, vec._stl_vec, element);
        });
        cls.method("pop_back", [](VectorWrapper& vec) {
            vec._pop_back(vec._stl_vec);
        });
        cls.method("insert", [](VectorWrapper& vec, size_t index, JsValue value) {
            vec._insert(vec._ctx, vec._stl_vec, index, value);
        });
        cls.method("erase", [](VectorWrapper& vec, size_t index) {
            vec._erase(vec._stl_vec, index);
        });
        cls.method("clear", [](VectorWrapper& vec) {
            vec._clear(vec._stl_vec);
        });
        cls.method("size", [](const VectorWrapper& vec) -> size_t {
            return vec._size(vec._stl_vec);
        });
        cls.method("empty", [](const VectorWrapper& vec) -> bool {
            return vec._empty(vec._stl_vec);
        });
        cls.method("reserve", [](VectorWrapper& vec, size_t cap) {
            vec._reserve(vec._stl_vec, cap);
        });
        cls.method("shrink_to_fit", [](VectorWrapper& vec) {
            vec._shrink_to_fit(vec._stl_vec);
        });
        cls.method("toJSArray", [](const VectorWrapper& vec) -> JsValue {
            return vec._to_js_array(vec._ctx, vec._stl_vec);
        });
        cls.method("forEach", [](VectorWrapper& vec, JsValue callback) {
            vec._for_each(vec._ctx, vec._stl_vec, callback);
        });
        cls.method("map", [](VectorWrapper& vec, JsValue callback) -> JsValue {
            JSContext* ctx = vec._ctx;
            size_t sz = vec._size(vec._stl_vec);
            JSValue arr = JS_NewArray(ctx);
            for (size_t i = 0; i < sz; ++i) {
                JsValue elem = vec._get(ctx, vec._stl_vec, i);
                JSValue idx = JS_NewInt64(ctx, static_cast<int64_t>(i));
                JSValue argv[] = {elem.value(), idx};
                JSValue result = JS_Call(ctx, callback.value(), JS_UNDEFINED, 2, argv);
                JS_FreeValue(ctx, idx);
                if (JS_IsException(result)) {
                    JS_FreeValue(ctx, arr);
                    return JsValue::adopt(ctx, result);
                }
                JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i), result);
            }
            return JsValue::adopt(ctx, arr);
        });
        cls.method("filter", [](VectorWrapper& vec, JsValue callback) -> JsValue {
            JSContext* ctx = vec._ctx;
            size_t sz = vec._size(vec._stl_vec);
            JSValue arr = JS_NewArray(ctx);
            uint32_t j = 0;
            for (size_t i = 0; i < sz; ++i) {
                JsValue elem = vec._get(ctx, vec._stl_vec, i);
                JSValue idx = JS_NewInt64(ctx, static_cast<int64_t>(i));
                JSValue argv[] = {elem.value(), idx};
                JSValue result = JS_Call(ctx, callback.value(), JS_UNDEFINED, 2, argv);
                JS_FreeValue(ctx, idx);
                if (JS_IsException(result)) {
                    JS_FreeValue(ctx, arr);
                    return JsValue::adopt(ctx, result);
                }
                bool keep = JS_ToBool(ctx, result);
                JS_FreeValue(ctx, result);
                if (keep) {
                    JS_SetPropertyUint32(ctx, arr, j++, JS_DupValue(ctx, elem.release()));
                }
            }
            return JsValue::adopt(ctx, arr);
        });
        cls.method("reduce", [](VectorWrapper& vec, JsValue callback, JsValue initialValue) -> JsValue {
            JSContext* ctx = vec._ctx;
            size_t sz = vec._size(vec._stl_vec);
            JSValue accumulator = initialValue.isNull() ? JS_UNDEFINED : JS_DupValue(ctx, initialValue.value());
            size_t startIndex = 0;
            if (JS_IsUndefined(accumulator) && sz > 0) {
                accumulator = vec._get(ctx, vec._stl_vec, 0).release();
                startIndex = 1;
            }
            for (size_t i = startIndex; i < sz; ++i) {
                JsValue elem = vec._get(ctx, vec._stl_vec, i);
                JSValue idx = JS_NewInt64(ctx, static_cast<int64_t>(i));
                JSValue argv[] = {accumulator, elem.value(), idx};
                JSValue result = JS_Call(ctx, callback.value(), JS_UNDEFINED, 3, argv);
                JS_FreeValue(ctx, idx);
                JS_FreeValue(ctx, accumulator);
                if (JS_IsException(result)) {
                    return JsValue::adopt(ctx, result);;
                }
                accumulator = result;
            }
            return JsValue::adopt(ctx, accumulator);
        });
        cls.method("find", [](VectorWrapper& vec, JsValue callback) -> JsValue {
            JSContext* ctx = vec._ctx;
            size_t sz = vec._size(vec._stl_vec);
            for (size_t i = 0; i < sz; ++i) {
                JsValue elem = vec._get(ctx, vec._stl_vec, i);
                JSValue idx = JS_NewInt64(ctx, static_cast<int64_t>(i));
                JSValue argv[] = {elem.value(), idx};
                JSValue result = JS_Call(ctx, callback.value(), JS_UNDEFINED, 2, argv);
                JS_FreeValue(ctx, idx);
                if (JS_IsException(result)) {
                    return JsValue::adopt(ctx, result);
                }
                bool found = JS_ToBool(ctx, result);
                JS_FreeValue(ctx, result);
                if (found) {
                    return elem;
                }
            }
            return JsValue(ctx);
        });
        cls.method("findIndex", [](VectorWrapper& vec, JsValue callback) -> int64_t {
            JSContext* ctx = vec._ctx;
            size_t sz = vec._size(vec._stl_vec);
            for (size_t i = 0; i < sz; ++i) {
                JsValue elem = vec._get(ctx, vec._stl_vec, i);
                JSValue idx = JS_NewInt64(ctx, static_cast<int64_t>(i));
                JSValue argv[] = {elem.value(), idx};
                JSValue result = JS_Call(ctx, callback.value(), JS_UNDEFINED, 2, argv);
                JS_FreeValue(ctx, idx);
                if (JS_IsException(result)) {
                    JS_FreeValue(ctx, result);
                    return -1;
                }
                bool found = JS_ToBool(ctx, result);
                JS_FreeValue(ctx, result);
                if (found) {
                    return static_cast<int64_t>(i);
                }
            }
            return -1;
        });
        cls.method("some", [](VectorWrapper& vec, JsValue callback) -> bool {
            JSContext* ctx = vec._ctx;
            size_t sz = vec._size(vec._stl_vec);
            for (size_t i = 0; i < sz; ++i) {
                JsValue elem = vec._get(ctx, vec._stl_vec, i);
                JSValue idx = JS_NewInt64(ctx, static_cast<int64_t>(i));
                JSValue argv[] = {elem.value(), idx};
                JSValue result = JS_Call(ctx, callback.value(), JS_UNDEFINED, 2, argv);
                JS_FreeValue(ctx, idx);
                if (JS_IsException(result)) {
                    JS_FreeValue(ctx, result);
                    return false;
                }
                bool passed = JS_ToBool(ctx, result);
                JS_FreeValue(ctx, result);
                if (passed) {
                    return true;
                }
            }
            return false;
        });
        cls.method("every", [](VectorWrapper& vec, JsValue callback) -> bool {
            JSContext* ctx = vec._ctx;
            size_t sz = vec._size(vec._stl_vec);
            for (size_t i = 0; i < sz; ++i) {
                JsValue elem = vec._get(ctx, vec._stl_vec, i);
                JSValue idx = JS_NewInt64(ctx, static_cast<int64_t>(i));
                JSValue argv[] = {elem.value(), idx};
                JSValue result = JS_Call(ctx, callback.value(), JS_UNDEFINED, 2, argv);
                JS_FreeValue(ctx, idx);
                if (JS_IsException(result)) {
                    JS_FreeValue(ctx, result);
                    return false;
                }
                bool passed = JS_ToBool(ctx, result);
                JS_FreeValue(ctx, result);
                if (!passed) {
                    return false;
                }
            }
            return true;
        });
        cls.method("includes", [](VectorWrapper& vec, JsValue searchElement, int32_t fromIndex) -> bool {
            JSContext* ctx = vec._ctx;
            size_t sz = vec._size(vec._stl_vec);
            size_t start = fromIndex < 0 ? 
                static_cast<size_t>(std::max(0, static_cast<int32_t>(sz) + fromIndex)) : 
                static_cast<size_t>(fromIndex);
            for (size_t i = start; i < sz; ++i) {
                JsValue elem = vec._get(ctx, vec._stl_vec, i);
                bool equal = JS_IsStrictEqual(ctx, elem.value(), searchElement.value());
                if (equal) {
                    return true;
                }
            }
            return false;
        });
        cls.method("indexOf", [](VectorWrapper& vec, JsValue searchElement, int32_t fromIndex) -> int32_t {
            JSContext* ctx = vec._ctx;
            size_t sz = vec._size(vec._stl_vec);
            size_t start = fromIndex < 0 ? 
                static_cast<size_t>(std::max(0, static_cast<int32_t>(sz) + fromIndex)) : 
                static_cast<size_t>(fromIndex);
            for (size_t i = start; i < sz; ++i) {
                JsValue elem = vec._get(ctx, vec._stl_vec, i);
                bool equal = JS_IsStrictEqual(ctx, elem.value(), searchElement.value());
                if (equal) {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        });
        cls.method("slice", [](VectorWrapper& vec, int32_t start, int32_t end) -> JsValue {
            JSContext* ctx = vec._ctx;
            size_t sz = vec._size(vec._stl_vec);
            
            int32_t from = start < 0 ? std::max(0, static_cast<int32_t>(sz) + start) : std::min(start, static_cast<int32_t>(sz));
            int32_t to = end < 0 ? std::max(0, static_cast<int32_t>(sz) + end) : 
                         (end < 0 ? static_cast<int32_t>(sz) : std::min(end, static_cast<int32_t>(sz)));
            if (to < from) to = from;
            
            JSValue arr = JS_NewArray(ctx);
            for (int32_t i = from; i < to; ++i) {
                JsValue elem = vec._get(ctx, vec._stl_vec, static_cast<size_t>(i));
                JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i - from), elem.release());
            }
            return JsValue::adopt(ctx, arr);
        });
        cls.method("splice", [](VectorWrapper& vec, int32_t start, int32_t deleteCount, JsValue replacementValue) -> JsValue {
            JSContext* ctx = vec._ctx;
            size_t sz = vec._size(vec._stl_vec);
            
            int32_t from = start < 0 ? std::max(0, static_cast<int32_t>(sz) + start) : std::min(start, static_cast<int32_t>(sz));
            int32_t deleteCountActual = deleteCount < 0 ? 0 : 
                std::min(deleteCount, static_cast<int32_t>(sz) - from);
            
            JSValue removedArr = JS_NewArray(ctx);
            for (int32_t i = 0; i < deleteCountActual; ++i) {
                JsValue elem = vec._get(ctx, vec._stl_vec, static_cast<size_t>(from + i));
                JS_SetPropertyUint32(ctx, removedArr, static_cast<uint32_t>(i), elem.release());
            }
            
            for (int32_t i = 0; i < deleteCountActual; ++i) {
                vec._erase(vec._stl_vec, static_cast<size_t>(from));
            }
            
            if (!replacementValue.isNull()) {
                vec._insert(ctx, vec._stl_vec, static_cast<size_t>(from), replacementValue);
            }
            
            return JsValue::adopt(ctx, removedArr);
        });
        cls.method("join", [](VectorWrapper& vec, JsValue separator) -> std::string {
            JSContext* ctx = vec._ctx;
            size_t sz = vec._size(vec._stl_vec);
            std::string sep;
            if (separator.isNull()) {
                sep = ",";
            } else {
                JSValue sepStr = JS_ToString(ctx, separator.value());
                const char* sepCStr = JS_ToCString(ctx, sepStr);
                if (sepCStr) {
                    sep = sepCStr;
                    JS_FreeCString(ctx, sepCStr);
                }
                JS_FreeValue(ctx, sepStr);
            }
            
            std::string result;
            for (size_t i = 0; i < sz; ++i) {
                if (i > 0) result += sep;
                JsValue elem = vec._get(ctx, vec._stl_vec, i);
                JSValue elemStr = JS_ToString(ctx, elem.value());
                const char* elemCStr = JS_ToCString(ctx, elemStr);
                if (elemCStr) {
                    result += elemCStr;
                    JS_FreeCString(ctx, elemCStr);
                }
                JS_FreeValue(ctx, elemStr);
            }
            return result;
        });
        cls.method("reverse", [](VectorWrapper& vec) -> JsValue {
            return vec._to_js_array(vec._ctx, vec._stl_vec);
        });
        cls.method("toString", [](const VectorWrapper& vec) -> std::string {
            return "VectorWrapper[size=" + std::to_string(vec._size(vec._stl_vec)) + "]";
        });
    }

    VectorWrapper(JSContext* ctx, void* stl_vec,
                  GetElement get, SetElement set,
                  PushBack push_back, PopBack pop_back,
                  Insert insert, Erase erase,
                  Clear clear, Size size, Empty empty,
                  Reserve reserve, ShrinkToFit shrink_to_fit,
                  ToJSArray to_js_array, ForEach for_each)
        : _ctx(ctx), _stl_vec(stl_vec)
        , _get(get), _set(set)
        , _push_back(push_back), _pop_back(pop_back)
        , _insert(insert), _erase(erase)
        , _clear(clear), _size(size), _empty(empty)
        , _reserve(reserve), _shrink_to_fit(shrink_to_fit)
        , _to_js_array(to_js_array), _for_each(for_each)
    {}

    ~VectorWrapper() = default;
    VectorWrapper(const VectorWrapper&) = delete;
    VectorWrapper& operator=(const VectorWrapper&) = delete;
    JSContext* context() const noexcept { return _ctx; }

private:
    bool resolveIndex(int32_t index, size_t& out) const {
        size_t sz = _size(_stl_vec);
        if (index < 0) {
            int32_t resolved = static_cast<int32_t>(sz) + index;
            if (resolved < 0) return false;
            out = static_cast<size_t>(resolved);
            return true;
        }
        out = static_cast<size_t>(index);
        return out < sz;
    }

    JsValue getAt(int32_t index) {
        size_t idx;
        if (!resolveIndex(index, idx)) return JsValue(_ctx);
        return _get(_ctx, _stl_vec, idx);
    }

    void setAt(int32_t index, JsValue value) {
        size_t idx;
        if (!resolveIndex(index, idx)) return;
        _set(_ctx, _stl_vec, idx, value);
    }

    JSContext*  _ctx = nullptr;
    void*       _stl_vec = nullptr;
    GetElement   _get = nullptr;
    SetElement   _set = nullptr;
    PushBack     _push_back = nullptr;
    PopBack      _pop_back = nullptr;
    Insert       _insert = nullptr;
    Erase        _erase = nullptr;
    Clear        _clear = nullptr;
    Size         _size = nullptr;
    Empty        _empty = nullptr;
    Reserve      _reserve = nullptr;
    ShrinkToFit  _shrink_to_fit = nullptr;
    ToJSArray    _to_js_array = nullptr;
    ForEach      _for_each = nullptr;
};

template <typename T>
VectorWrapper* makeVectorWrapper(JSContext* ctx, std::vector<T>* vec) {
    return new VectorWrapper(
        ctx, static_cast<void*>(vec),
        /* get */ [](JSContext* c, void* v, size_t i) -> JsValue {
            auto* p = static_cast<std::vector<T>*>(v);
            return JsValue::adopt(c, JsConverter<T>::toJs(c, (*p)[i]));
        },
        /* set */ [](JSContext* c, void* v, size_t i, JsValue val) {
            auto* p = static_cast<std::vector<T>*>(v);
            (*p)[i] = JsConverter<T>::fromJs(c, val.value());
        },
        /* push_back */ [](JSContext* c, void* v, JsValue val) {
            static_cast<std::vector<T>*>(v)->push_back(JsConverter<T>::fromJs(c, val.value()));
        },
        /* pop_back */ [](void* v) {
            static_cast<std::vector<T>*>(v)->pop_back();
        },
        /* insert */ [](JSContext* c, void* v, size_t i, JsValue val) {
            auto* p = static_cast<std::vector<T>*>(v);
            p->insert(p->begin() + static_cast<ptrdiff_t>(i),
                      JsConverter<T>::fromJs(c, val.value()));
        },
        /* erase */ [](void* v, size_t i) {
            auto* p = static_cast<std::vector<T>*>(v);
            p->erase(p->begin() + static_cast<ptrdiff_t>(i));
        },
        /* clear */ [](void* v) {
            static_cast<std::vector<T>*>(v)->clear();
        },
        /* size */ [](void* v) -> size_t {
            return static_cast<std::vector<T>*>(v)->size();
        },
        /* empty */ [](void* v) -> bool {
            return static_cast<std::vector<T>*>(v)->empty();
        },
        /* reserve */ [](void* v, size_t cap) {
            static_cast<std::vector<T>*>(v)->reserve(cap);
        },
        /* shrink_to_fit */ [](void* v) {
            static_cast<std::vector<T>*>(v)->shrink_to_fit();
        },
        /* toJSArray */ [](JSContext* c, void* v) -> JsValue {
            return JsValue::adopt(c,
                JsConverter<std::vector<T>>::toJs(c, *static_cast<std::vector<T>*>(v)));
        },
        /* forEach */ [](JSContext* c, void* v, JsValue callback) {
            auto* p = static_cast<std::vector<T>*>(v);
            for (size_t i = 0; i < p->size(); ++i) {
                JsValue elem = JsValue::adopt(c, JsConverter<T&>::toJs(c, (*p)[i]));
                JSValue idx  = JS_NewInt64(c, static_cast<int64_t>(i));
                JSValue argv[] = {elem.value(), idx};
                JSValue result = JS_Call(c, callback.value(), JS_UNDEFINED, 2, argv);
                JS_FreeValue(c, idx);
                if (JS_IsException(result)) { JS_FreeValue(c, result); return; }
                JS_FreeValue(c, result);
            }
        }
    );
}

// ============================================================================
// MapWrapper — std::map<K,V> / std::unordered_map<K,V>
// ============================================================================
//
// JS API:
//   map.get(key)        map.set(key, val)
//   map.has(key)        map.delete(key)       map.clear()
//   map.size()          map.empty()
//   map.keys()          map.values()
//   map.forEach(cb)     // callback(value, key)
//   map.toString()

class MapWrapper {
public:
    using Get           = JsValue (*)(JSContext* ctx, void* stl_map, JsValue key);
    using Set           = void    (*)(JSContext* ctx, void* stl_map, JsValue key, JsValue value);
    using Contains      = bool    (*)(JSContext* ctx, void* stl_map, JsValue key);
    using Erase         = size_t  (*)(JSContext* ctx, void* stl_map, JsValue key);
    using Clear         = void    (*)(void* stl_map);
    using Size          = size_t  (*)(void* stl_map);
    using Empty         = bool    (*)(void* stl_map);
    using Keys          = JsValue (*)(JSContext* ctx, void* stl_map);
    using Values        = JsValue (*)(JSContext* ctx, void* stl_map);
    using ForEach       = void    (*)(JSContext* ctx, void* stl_map, JsValue callback);

    static constexpr const char* className() { return "MapWrapper"; }

    static void bind(ClassBinder<MapWrapper>& cls)
    {
        cls.method("get", [](MapWrapper& m, JsValue key) -> JsValue {
            return m._get(m._ctx, m._stl_map, key);
        });
        cls.method("set", [](MapWrapper& m, JsValue key, JsValue value) {
            m._set(m._ctx, m._stl_map, key, value);
        });
        cls.method("has", [](MapWrapper& m, JsValue key) -> bool {
            return m._contains(m._ctx, m._stl_map, key);
        });
        // Named "delete_" because "delete" is a C++ keyword.
        cls.method("delete_", [](MapWrapper& m, JsValue key) -> bool {
            return m._erase(m._ctx, m._stl_map, key) > 0;
        });
        cls.method("clear", [](MapWrapper& m) {
            m._clear(m._stl_map);
        });
        cls.method("size", [](const MapWrapper& m) -> size_t {
            return m._size(m._stl_map);
        });
        cls.method("empty", [](const MapWrapper& m) -> bool {
            return m._empty(m._stl_map);
        });
        // Returns a snapshot of keys as a native JS array.
        cls.method("keys", [](const MapWrapper& m) -> JsValue {
            return m._keys(m._ctx, m._stl_map);
        });
        // Returns a snapshot of values as a native JS array.
        cls.method("values", [](const MapWrapper& m) -> JsValue {
            return m._values(m._ctx, m._stl_map);
        });
        // Calls callback(value, key) for each entry.
        cls.method("forEach", [](MapWrapper& m, JsValue callback) {
            m._for_each(m._ctx, m._stl_map, callback);
        });
        cls.method("map", [](MapWrapper& m, JsValue callback) -> JsValue {
            JSContext* ctx = m._ctx;
            JSValue keysArr = m._keys(ctx, m._stl_map).value();
            JSValue valuesArr = m._values(ctx, m._stl_map).value();
            int32_t len = (int32_t)m._size(m._stl_map);
            JSValue arr = JS_NewArray(ctx);
            for (int32_t i = 0; i < len; ++i) {
                JSValue key = JS_GetPropertyUint32(ctx, keysArr, static_cast<uint32_t>(i));
                JSValue val = JS_GetPropertyUint32(ctx, valuesArr, static_cast<uint32_t>(i));
                JSValue argv[] = {val, key};
                JSValue result = JS_Call(ctx, callback.value(), JS_UNDEFINED, 2, argv);
                JS_FreeValue(ctx, key);
                JS_FreeValue(ctx, val);
                if (JS_IsException(result)) {
                    JS_FreeValue(ctx, keysArr);
                    JS_FreeValue(ctx, valuesArr);
                    JS_FreeValue(ctx, arr);
                    return JsValue::adopt(ctx, result);
                }
                JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i), result);
            }
            JS_FreeValue(ctx, keysArr);
            JS_FreeValue(ctx, valuesArr);
            return JsValue::adopt(ctx, arr);
        });
        cls.method("filter", [](MapWrapper& m, JsValue callback) -> JsValue {
            JSContext* ctx = m._ctx;
            JSValue keysArr = m._keys(ctx, m._stl_map).value();
            JSValue valuesArr = m._values(ctx, m._stl_map).value();
            int32_t len = (int32_t)m._size(m._stl_map);
            JSValue arr = JS_NewArray(ctx);
            uint32_t j = 0;
            for (int32_t i = 0; i < len; ++i) {
                JSValue key = JS_GetPropertyUint32(ctx, keysArr, static_cast<uint32_t>(i));
                JSValue val = JS_GetPropertyUint32(ctx, valuesArr, static_cast<uint32_t>(i));
                JSValue argv[] = {val, key};
                JSValue result = JS_Call(ctx, callback.value(), JS_UNDEFINED, 2, argv);
                JS_FreeValue(ctx, key);
                JS_FreeValue(ctx, val);
                if (JS_IsException(result)) {
                    JS_FreeValue(ctx, keysArr);
                    JS_FreeValue(ctx, valuesArr);
                    JS_FreeValue(ctx, arr);
                    return JsValue::adopt(ctx, result);
                }
                bool keep = JS_ToBool(ctx, result);
                JS_FreeValue(ctx, result);
                if (keep) {
                    JSValue entry = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, entry, "key", JS_GetPropertyUint32(ctx, keysArr, static_cast<uint32_t>(i)));
                    JS_SetPropertyStr(ctx, entry, "value", JS_GetPropertyUint32(ctx, valuesArr, static_cast<uint32_t>(i)));
                    JS_SetPropertyUint32(ctx, arr, j++, entry);
                }
            }
            JS_FreeValue(ctx, keysArr);
            JS_FreeValue(ctx, valuesArr);
            return JsValue::adopt(ctx, arr);
        });
        cls.method("reduce", [](MapWrapper& m, JsValue callback, JsValue initialValue) -> JsValue {
            JSContext* ctx = m._ctx;
            JSValue keysArr = m._keys(ctx, m._stl_map).value();
            JSValue valuesArr = m._values(ctx, m._stl_map).value();
            int32_t len = (int32_t)m._size(m._stl_map);
            JSValue accumulator = initialValue.isNull() ? JS_UNDEFINED : JS_DupValue(ctx, initialValue.value());
            int32_t startIndex = 0;
            if (JS_IsUndefined(accumulator) && len > 0) {
                accumulator = JS_GetPropertyUint32(ctx, valuesArr, 0);
                startIndex = 1;
            }
            for (int32_t i = startIndex; i < len; ++i) {
                JSValue key = JS_GetPropertyUint32(ctx, keysArr, static_cast<uint32_t>(i));
                JSValue val = JS_GetPropertyUint32(ctx, valuesArr, static_cast<uint32_t>(i));
                JSValue argv[] = {accumulator, val, key};
                JSValue result = JS_Call(ctx, callback.value(), JS_UNDEFINED, 3, argv);
                JS_FreeValue(ctx, key);
                JS_FreeValue(ctx, val);
                JS_FreeValue(ctx, accumulator);
                if (JS_IsException(result)) {
                    JS_FreeValue(ctx, keysArr);
                    JS_FreeValue(ctx, valuesArr);
                    return JsValue::adopt(ctx, result);
                }
                accumulator = result;
            }
            JS_FreeValue(ctx, keysArr);
            JS_FreeValue(ctx, valuesArr);
            return JsValue::adopt(ctx, accumulator);
        });
        cls.method("toString", [](const MapWrapper& m) -> std::string {
            return "MapWrapper[size=" + std::to_string(m._size(m._stl_map)) + "]";
        });
    }

    MapWrapper(JSContext* ctx, void* stl_map,
               Get get, Set set, Contains contains, Erase erase,
               Clear clear, Size size, Empty empty,
               Keys keys, Values values, ForEach for_each)
        : _ctx(ctx), _stl_map(stl_map)
        , _get(get), _set(set), _contains(contains), _erase(erase)
        , _clear(clear), _size(size), _empty(empty)
        , _keys(keys), _values(values), _for_each(for_each)
    {}

    ~MapWrapper() = default;
    MapWrapper(const MapWrapper&) = delete;
    MapWrapper& operator=(const MapWrapper&) = delete;
    JSContext* context() const noexcept { return _ctx; }

private:
    JSContext*  _ctx = nullptr;
    void*       _stl_map = nullptr;
    Get         _get = nullptr;
    Set         _set = nullptr;
    Contains    _contains = nullptr;
    Erase       _erase = nullptr;
    Clear       _clear = nullptr;
    Size        _size = nullptr;
    Empty       _empty = nullptr;
    Keys        _keys = nullptr;
    Values      _values = nullptr;
    ForEach     _for_each = nullptr;
};

// --- Helper: construct a key from JS for map/set lookups -------------------

namespace detail {

template <typename Container>
auto makeKey(JSContext* ctx, JsValue jsKey) {
    using Key = typename Container::key_type;
    return JsConverter<Key>::fromJs(ctx, jsKey.value());
}

} // namespace detail

// --- makeMapWrapper for associative containers (map / unordered_map) -------

/**
 * @brief Create a MapWrapper for any associative container with pair<K,V> values.
 *
 * Works with std::map<K,V>, std::unordered_map<K,V>, and any container
 * providing the same interface (key_type, mapped_type, find, insert, erase, etc.).
 */
template <typename Map>
MapWrapper* makeMapWrapper(JSContext* ctx, Map* m) {
    using Key   = typename Map::key_type;
    using Mapped = typename Map::mapped_type;

    return new MapWrapper(
        ctx, static_cast<void*>(m),

        /* get */ [](JSContext* c, void* v, JsValue k) -> JsValue {
            auto* map = static_cast<Map*>(v);
            auto it = map->find(detail::makeKey<Map>(c, k));
            if (it == map->end()) return JsValue(c); // undefined
            return JsValue::adopt(c, JsConverter<Mapped>::toJs(c, it->second));
        },

        /* set */ [](JSContext* c, void* v, JsValue k, JsValue val) {
            auto* map = static_cast<Map*>(v);
            (*map)[detail::makeKey<Map>(c, k)] = JsConverter<Mapped>::fromJs(c, val.value());
        },

        /* contains */ [](JSContext* c, void* v, JsValue k) -> bool {
            auto* map = static_cast<Map*>(v);
            return map->find(detail::makeKey<Map>(c, k)) != map->end();
        },

        /* erase — returns number of elements removed */ [](JSContext* c, void* v, JsValue k) -> size_t {
            auto* map = static_cast<Map*>(v);
            return map->erase(detail::makeKey<Map>(c, k));
        },

        /* clear */ [](void* v) {
            static_cast<Map*>(v)->clear();
        },

        /* size */ [](void* v) -> size_t {
            return static_cast<Map*>(v)->size();
        },

        /* empty */ [](void* v) -> bool {
            return static_cast<Map*>(v)->empty();
        },

        /* keys */ [](JSContext* c, void* v) -> JsValue {
            auto* map = static_cast<Map*>(v);
            JSValue arr = JS_NewArray(c);
            uint32_t i = 0;
            for (auto& [key, _] : *map) {
                JS_SetPropertyUint32(c, arr, i++, JsConverter<Key>::toJs(c, key));
            }
            return JsValue::adopt(c, arr);
        },

        /* values */ [](JSContext* c, void* v) -> JsValue {
            auto* map = static_cast<Map*>(v);
            JSValue arr = JS_NewArray(c);
            uint32_t i = 0;
            for (auto& [_, val] : *map) {
                JS_SetPropertyUint32(c, arr, i++, JsConverter<Mapped>::toJs(c, val));
            }
            return JsValue::adopt(c, arr);
        },

        /* forEach — callback(value, key) */ [](JSContext* c, void* v, JsValue callback) {
            auto* map = static_cast<Map*>(v);
            for (auto& [key, val] : *map) {
                JSValue jsVal = JsConverter<Mapped&>::toJs(c, val);
                JSValue jsKey = JsConverter<Key&>::toJs(c, key);
                JSValue argv[] = {jsVal, jsKey};
                JSValue result = JS_Call(c, callback.value(), JS_UNDEFINED, 2, argv);
                JS_FreeValue(c, jsVal);
                JS_FreeValue(c, jsKey);
                if (JS_IsException(result)) { JS_FreeValue(c, result); return; }
                JS_FreeValue(c, result);
            }
        }
    );
}

/// Convenience: std::map<K,V>
template <typename K, typename V>
MapWrapper* makeMapWrapper(JSContext* ctx, std::map<K, V>* m) {
    return makeMapWrapper<std::map<K, V>>(ctx, m);
}

/// Convenience: std::unordered_map<K,V>
template <typename K, typename V>
MapWrapper* makeMapWrapper(JSContext* ctx, std::unordered_map<K, V>* m) {
    return makeMapWrapper<std::unordered_map<K, V>>(ctx, m);
}

// ============================================================================
// SetWrapper — std::set<K> / std::unordered_set<K>
// ============================================================================
//
// JS API:
//   set.has(key)        set.add(key)           set.delete_(key)
//   set.clear()          set.size()             set.empty()
//   set.forEach(cb)      // callback(key)
//   set.toJSArray()      set.toString()

class SetWrapper {
public:
    using Has           = bool    (*)(JSContext* ctx, void* stl_set, JsValue key);
    using Add           = void    (*)(JSContext* ctx, void* stl_set, JsValue key);
    using Erase         = size_t  (*)(JSContext* ctx, void* stl_set, JsValue key);
    using Clear         = void    (*)(void* stl_set);
    using Size          = size_t  (*)(void* stl_set);
    using Empty         = bool    (*)(void* stl_set);
    using ToJSArray     = JsValue (*)(JSContext* ctx, void* stl_set);
    using ForEach       = void    (*)(JSContext* ctx, void* stl_set, JsValue callback);

    static constexpr const char* className() { return "SetWrapper"; }

    static void bind(ClassBinder<SetWrapper>& cls)
    {
        cls.method("has", [](SetWrapper& s, JsValue key) -> bool {
            return s._has(s._ctx, s._stl_set, key);
        });
        cls.method("add", [](SetWrapper& s, JsValue key) {
            s._add(s._ctx, s._stl_set, key);
        });
        cls.method("delete_", [](SetWrapper& s, JsValue key) -> bool {
            return s._erase(s._ctx, s._stl_set, key) > 0;
        });
        cls.method("clear", [](SetWrapper& s) {
            s._clear(s._stl_set);
        });
        cls.method("size", [](const SetWrapper& s) -> size_t {
            return s._size(s._stl_set);
        });
        cls.method("empty", [](const SetWrapper& s) -> bool {
            return s._empty(s._stl_set);
        });
        cls.method("toJSArray", [](const SetWrapper& s) -> JsValue {
            return s._to_js_array(s._ctx, s._stl_set);
        });
        cls.method("forEach", [](SetWrapper& s, JsValue callback) {
            s._for_each(s._ctx, s._stl_set, callback);
        });
        cls.method("map", [](SetWrapper& s, JsValue callback) -> JsValue {
            JSContext* ctx = s._ctx;
            JSValue arr = s._to_js_array(ctx, s._stl_set).value();
            int32_t len = (int32_t)s._size(s._stl_set);
            JSValue resultArr = JS_NewArray(ctx);
            for (int32_t i = 0; i < len; ++i) {
                JsValue elem = JsValue::adopt(ctx, JS_GetPropertyUint32(ctx, arr, static_cast<uint32_t>(i)));
                JSValue argv[] = {elem.value()};
                JSValue result = JS_Call(ctx, callback.value(), JS_UNDEFINED, 1, argv);
                if (JS_IsException(result)) {
                    JS_FreeValue(ctx, arr);
                    JS_FreeValue(ctx, resultArr);
                    return JsValue::adopt(ctx, result);
                }
                JS_SetPropertyUint32(ctx, resultArr, static_cast<uint32_t>(i), result);
            }
            JS_FreeValue(ctx, arr);
            return JsValue::adopt(ctx, resultArr);
        });
        cls.method("filter", [](SetWrapper& s, JsValue callback) -> JsValue {
            JSContext* ctx = s._ctx;
            JSValue arr = s._to_js_array(ctx, s._stl_set).value();
            int32_t len = (int32_t)s._size(s._stl_set);
            JSValue resultArr = JS_NewArray(ctx);
            uint32_t j = 0;
            for (int32_t i = 0; i < len; ++i) {
                JsValue elem = JsValue::adopt(ctx, JS_GetPropertyUint32(ctx, arr, static_cast<uint32_t>(i)));
                JSValue argv[] = {elem.value()};
                JSValue result = JS_Call(ctx, callback.value(), JS_UNDEFINED, 1, argv);
                if (JS_IsException(result)) {
                    JS_FreeValue(ctx, arr);
                    JS_FreeValue(ctx, resultArr);
                    return JsValue::adopt(ctx, result);
                }
                bool keep = JS_ToBool(ctx, result);
                JS_FreeValue(ctx, result);
                if (keep) {
                    JS_SetPropertyUint32(ctx, resultArr, j++, JS_DupValue(ctx, elem.release()));
                }
            }
            JS_FreeValue(ctx, arr);
            return JsValue::adopt(ctx, resultArr);
        });
        cls.method("reduce", [](SetWrapper& s, JsValue callback, JsValue initialValue) -> JsValue {
            JSContext* ctx = s._ctx;
            JSValue arr = s._to_js_array(ctx, s._stl_set).value();
            int32_t len = (int32_t)s._size(s._stl_set);
            JSValue accumulator = initialValue.isNull() ? JS_UNDEFINED : JS_DupValue(ctx, initialValue.value());
            int32_t startIndex = 0;
            if (JS_IsUndefined(accumulator) && len > 0) {
                accumulator = JS_GetPropertyUint32(ctx, arr, 0);
                startIndex = 1;
            }
            for (int32_t i = startIndex; i < len; ++i) {
                JsValue elem = JsValue::adopt(ctx, JS_GetPropertyUint32(ctx, arr, static_cast<uint32_t>(i)));
                JSValue argv[] = {accumulator, elem.value()};
                JSValue result = JS_Call(ctx, callback.value(), JS_UNDEFINED, 2, argv);
                JS_FreeValue(ctx, accumulator);
                if (JS_IsException(result)) {
                    JS_FreeValue(ctx, arr);
                    return JsValue::adopt(ctx, result);
                }
                accumulator = result;
            }
            JS_FreeValue(ctx, arr);
            return JsValue::adopt(ctx, accumulator);
        });
        cls.method("some", [](SetWrapper& s, JsValue callback) -> bool {
            JSContext* ctx = s._ctx;
            JSValue arr = s._to_js_array(ctx, s._stl_set).value();
            int32_t len = (int32_t)s._size(s._stl_set);
            for (int32_t i = 0; i < len; ++i) {
                JsValue elem = JsValue::adopt(ctx, JS_GetPropertyUint32(ctx, arr, static_cast<uint32_t>(i)));
                JSValue argv[] = {elem.value()};
                JSValue result = JS_Call(ctx, callback.value(), JS_UNDEFINED, 1, argv);
                if (JS_IsException(result)) {
                    JS_FreeValue(ctx, arr);
                    JS_FreeValue(ctx, result);
                    return false;
                }
                bool passed = JS_ToBool(ctx, result);
                JS_FreeValue(ctx, result);
                if (passed) {
                    JS_FreeValue(ctx, arr);
                    return true;
                }
            }
            JS_FreeValue(ctx, arr);
            return false;
        });
        cls.method("every", [](SetWrapper& s, JsValue callback) -> bool {
            JSContext* ctx = s._ctx;
            JSValue arr = s._to_js_array(ctx, s._stl_set).value();
            int32_t len = (int32_t)s._size(s._stl_set);
            for (int32_t i = 0; i < len; ++i) {
                JsValue elem = JsValue::adopt(ctx, JS_GetPropertyUint32(ctx, arr, static_cast<uint32_t>(i)));
                JSValue argv[] = {elem.value()};
                JSValue result = JS_Call(ctx, callback.value(), JS_UNDEFINED, 1, argv);
                if (JS_IsException(result)) {
                    JS_FreeValue(ctx, arr);
                    JS_FreeValue(ctx, result);
                    return false;
                }
                bool passed = JS_ToBool(ctx, result);
                JS_FreeValue(ctx, result);
                if (!passed) {
                    JS_FreeValue(ctx, arr);
                    return false;
                }
            }
            JS_FreeValue(ctx, arr);
            return true;
        });
        cls.method("toString", [](const SetWrapper& s) -> std::string {
            return "SetWrapper[size=" + std::to_string(s._size(s._stl_set)) + "]";
        });
    }

    SetWrapper(JSContext* ctx, void* stl_set,
               Has has, Add add, Erase erase,
               Clear clear, Size size, Empty empty,
               ToJSArray to_js_array, ForEach for_each)
        : _ctx(ctx), _stl_set(stl_set)
        , _has(has), _add(add), _erase(erase)
        , _clear(clear), _size(size), _empty(empty)
        , _to_js_array(to_js_array), _for_each(for_each)
    {}

    ~SetWrapper() = default;
    SetWrapper(const SetWrapper&) = delete;
    SetWrapper& operator=(const SetWrapper&) = delete;
    JSContext* context() const noexcept { return _ctx; }

private:
    JSContext*  _ctx = nullptr;
    void*       _stl_set = nullptr;
    Has         _has = nullptr;
    Add         _add = nullptr;
    Erase       _erase = nullptr;
    Clear       _clear = nullptr;
    Size        _size = nullptr;
    Empty       _empty = nullptr;
    ToJSArray   _to_js_array = nullptr;
    ForEach     _for_each = nullptr;
};

// --- makeSetWrapper for set containers (set / unordered_set) ---------------

/**
 * @brief Create a SetWrapper for any set-like container.
 *
 * Works with std::set<K>, std::unordered_set<K>, and any container providing
 * the same interface (key_type, find, insert, erase, etc.).
 */
template <typename Set>
SetWrapper* makeSetWrapper(JSContext* ctx, Set* s) {
    using Key = typename Set::key_type;

    return new SetWrapper(
        ctx, static_cast<void*>(s),

        /* has */ [](JSContext* c, void* v, JsValue k) -> bool {
            auto* set = static_cast<Set*>(v);
            return set->find(detail::makeKey<Set>(c, k)) != set->end();
        },

        /* add */ [](JSContext* c, void* v, JsValue k) {
            static_cast<Set*>(v)->insert(detail::makeKey<Set>(c, k));
        },

        /* erase — returns number of elements removed */ [](JSContext* c, void* v, JsValue k) -> size_t {
            return static_cast<Set*>(v)->erase(detail::makeKey<Set>(c, k));
        },

        /* clear */ [](void* v) {
            static_cast<Set*>(v)->clear();
        },

        /* size */ [](void* v) -> size_t {
            return static_cast<Set*>(v)->size();
        },

        /* empty */ [](void* v) -> bool {
            return static_cast<Set*>(v)->empty();
        },

        /* toJSArray */ [](JSContext* c, void* v) -> JsValue {
            auto* set = static_cast<Set*>(v);
            JSValue arr = JS_NewArray(c);
            uint32_t i = 0;
            for (auto& elem : *set) {
                JS_SetPropertyUint32(c, arr, i++, JsConverter<Key>::toJs(c, elem));
            }
            return JsValue::adopt(c, arr);
        },

        /* forEach — callback(key) */ [](JSContext* c, void* v, JsValue callback) {
            auto* set = static_cast<Set*>(v);
            for (auto& elem : *set) {
                JSValue jsKey = JsConverter<Key>::toJs(c, std::ref(elem));
                JSValue argv[] = {jsKey};
                JSValue result = JS_Call(c, callback.value(), JS_UNDEFINED, 1, argv);
                JS_FreeValue(c, jsKey);
                if (JS_IsException(result)) { JS_FreeValue(c, result); return; }
                JS_FreeValue(c, result);
            }
        }
    );
}

/// Convenience: std::set<K>
template <typename K>
SetWrapper* makeSetWrapper(JSContext* ctx, std::set<K>* s) {
    return makeSetWrapper<std::set<K>>(ctx, s);
}

/// Convenience: std::unordered_set<K>
template <typename K>
SetWrapper* makeSetWrapper(JSContext* ctx, std::unordered_set<K>* s) {
    return makeSetWrapper<std::unordered_set<K>>(ctx, s);
}

// ============================================================================
// Lazy auto-registration (once per JSContext)
// ============================================================================

namespace detail {

/**
 * @brief Ensure a wrapper type is registered and its prototype is set up
 *        for the given JSContext.
 *
 * This performs a one-time, per-context registration of the JSClass and its
 * prototype (with all methods bound).  The check is O(1): a single
 * JS_IsNull comparison on the cached class_proto value.
 *
 * Design goals:
 * - Zero overhead after first registration (single branch on per-cid state).
 * - No manual bind/install call needed by users.
 * - Thread-safe: relies on QuickJS's single-threaded context guarantee.
 * - The wrapper types are NOT constructible from JS (no constructor is
 *   installed); they can only be received from C++ via JsConverter.
 */
template <typename W>
inline void ensureWrapperBound(JSContext* ctx) {
    JSRuntime* rt = JS_GetRuntime(ctx);

    // Lazily allocate the JSClassID (global, once per type in the process).
    JSClassID cid = ClassRegistry::classId<W>(rt);

    // Quick check: if the prototype is already set, we're done.
    // JS_GetClassProto returns JS_NULL when the class was never installed.
    // This is O(1) — no atom lookup, no heap allocation.
    JSValue proto = JS_GetClassProto(ctx, cid);
    if (!JS_IsNull(proto)) {
        JS_FreeValue(ctx, proto);
        return;
    }
    JS_FreeValue(ctx, proto);

    // First time for this context — register the class and bind methods.
    // We use ClassBinder but call installInto() on a throwaway object
    // instead of install() (to avoid polluting the global namespace).
    // The constructor is intentionally NOT registered — wrapper objects
    // can only be created from C++.
    ClassBinder<W> binder(ctx, W::className());
    W::bind(binder);
    binder.install();

    // Install into a temporary object, then discard the constructor.
    // The side effect we need: buildConstructor() sets the class_proto
    // via JS_SetClassProto(ctx, class_id, proto).
    // JSValue tmp = JS_NewObject(ctx);
    // binder.installInto(tmp);
    // JS_FreeValue(ctx, tmp);
}

// makeWrapper dispatch — wraps the container creation call.
template <typename W, typename Container>
auto makeWrapper(JSContext* ctx, Container* c) {
    if constexpr (std::is_same_v<W, VectorWrapper>) {
        return makeVectorWrapper(ctx, c);
    } else if constexpr (std::is_same_v<W, MapWrapper>) {
        return makeMapWrapper(ctx, c);
    } else if constexpr (std::is_same_v<W, SetWrapper>) {
        return makeSetWrapper(ctx, c);
    }
}

/// Create a wrapper JSValue from a pointer to an STL container.
/// Handles lazy auto-registration transparently.
template <typename W, typename Container>
inline JSValue wrapContainer(JSContext* ctx, Container* container) {
    // Auto-register on first use (once per JSContext, amortized O(1)).
    detail::ensureWrapperBound<W>(ctx);

    JSRuntime* rt = JS_GetRuntime(ctx);
    JSClassID cid = ClassRegistry::classId<W>(rt);

    JSValue proto = JS_GetClassProto(ctx, cid);
    JSValue obj = JS_NewObjectProtoClass(ctx, proto, cid);
    JS_FreeValue(ctx, proto);

    auto* wrapper = makeWrapper<W>(ctx, container);
    auto* pd = ClassRegistry::makeOwned(wrapper);
    JS_SetOpaque(obj, pd);
    return obj;
}

} // namespace detail

// ============================================================================
// JsConverter specializations — pass STL containers to JS as wrappers
// ============================================================================
//
// Each specialization lazily auto-registers the corresponding wrapper class
// on first use (once per JSContext).  No manual bind/install needed.
// ============================================================================

template <typename T>
struct JsConverter<std::vector<T>&> {
    static JSValue toJs(JSContext* ctx, const std::vector<T>& vec) {
        return detail::wrapContainer<VectorWrapper>(
            ctx, const_cast<std::vector<T>*>(&vec));
    }
};
template <typename T>
struct JsConverter<std::vector<T>*> {
    static JSValue toJs(JSContext* ctx, const std::vector<T>* vec) {
        return JsConverter<std::vector<T>&>::toJs(ctx, *vec);
    }
};
template <typename T>
struct JsConverter<std::reference_wrapper<std::vector<T>>> {
    static JSValue toJs(JSContext* ctx, std::reference_wrapper<std::vector<T>> vec) {
        return JsConverter<std::vector<T>&>::toJs(ctx, vec);
    }
};
template <typename T>
struct JsConverter<std::reference_wrapper<const std::vector<T>>> {
    static JSValue toJs(JSContext* ctx, std::reference_wrapper<const std::vector<T>> vec) {
        return JsConverter<std::vector<T>&>::toJs(ctx, vec);
    }
};

template <typename K, typename V>
struct JsConverter<std::map<K, V>&> {
    static JSValue toJs(JSContext* ctx, const std::map<K, V>& m) {
        return detail::wrapContainer<MapWrapper>(
            ctx, const_cast<std::map<K, V>*>(&m));
    }
};
template <typename K, typename V>
struct JsConverter<std::map<K, V>*> {
    static JSValue toJs(JSContext* ctx, const std::map<K, V>* m) {
        return JsConverter<std::map<K, V>&>::toJs(ctx, *m);
    }
};
template <typename K, typename V>
struct JsConverter<std::reference_wrapper<std::map<K, V>>> {
    static JSValue toJs(JSContext* ctx, std::reference_wrapper<std::map<K, V>> m) {
        return JsConverter<std::map<K, V>&>::toJs(ctx, m);
    }
};
template <typename K, typename V>
struct JsConverter<std::reference_wrapper<const std::map<K, V>>> {
    static JSValue toJs(JSContext* ctx, std::reference_wrapper<const std::map<K, V>> m) {
        return JsConverter<std::map<K, V>&>::toJs(ctx, m);
    }
};

template <typename K, typename V>
struct JsConverter<std::unordered_map<K, V>&> {
    static JSValue toJs(JSContext* ctx, const std::unordered_map<K, V>& m) {
        return detail::wrapContainer<MapWrapper>(
            ctx, const_cast<std::unordered_map<K, V>*>(&m));
    }
};
template <typename K, typename V>
struct JsConverter<std::unordered_map<K, V>*> {
    static JSValue toJs(JSContext* ctx, const std::unordered_map<K, V>* m) {
        return JsConverter<std::unordered_map<K, V>&>::toJs(ctx, *m);
    }
};
template <typename K, typename V>
struct JsConverter<std::reference_wrapper<std::unordered_map<K, V>>> {
    static JSValue toJs(JSContext* ctx, std::reference_wrapper<std::unordered_map<K, V>> m) {
        return JsConverter<std::unordered_map<K, V>&>::toJs(ctx, m);
    }
};
template <typename K, typename V>
struct JsConverter<std::reference_wrapper<const std::unordered_map<K, V>>> {
    static JSValue toJs(JSContext* ctx, std::reference_wrapper<const std::unordered_map<K, V>> m) {
        return JsConverter<std::unordered_map<K, V>&>::toJs(ctx, m);
    }
};

template <typename K>
struct JsConverter<std::set<K>&> {
    static JSValue toJs(JSContext* ctx, const std::set<K>& s) {
        return detail::wrapContainer<SetWrapper>(
            ctx, const_cast<std::set<K>*>(&s));
    }
};
template <typename K>
struct JsConverter<std::set<K>*> {
    static JSValue toJs(JSContext* ctx, const std::set<K>* s) {
        return JsConverter<std::set<K>&>::toJs(ctx, *s);
    }
};
template <typename K>
struct JsConverter<const std::set<K>&> {
    static JSValue toJs(JSContext* ctx, const std::set<K>* s) {
        return JsConverter<std::set<K>&>::toJs(ctx, *s);
    }
};
template <typename K>
struct JsConverter<const std::set<K>*> {
    static JSValue toJs(JSContext* ctx, const std::set<K>* s) {
        return JsConverter<std::set<K>&>::toJs(ctx, *s);
    }
};
template <typename K>
struct JsConverter<std::reference_wrapper<std::set<K>>> {
    static JSValue toJs(JSContext* ctx, std::reference_wrapper<std::set<K>> s) {
        return JsConverter<std::set<K>&>::toJs(ctx, s);
    }
};
template <typename K>
struct JsConverter<std::reference_wrapper<const std::set<K>>> {
    static JSValue toJs(JSContext* ctx, std::reference_wrapper<const std::set<K>> s) {
        return JsConverter<std::set<K>&>::toJs(ctx, s);
    }
};

template <typename K>
struct JsConverter<std::unordered_set<K>&> {
    static JSValue toJs(JSContext* ctx, const std::unordered_set<K>& s) {
        return detail::wrapContainer<SetWrapper>(
            ctx, const_cast<std::unordered_set<K>*>(&s));
    }
};
template <typename K>
struct JsConverter<std::unordered_set<K>*> {
    static JSValue toJs(JSContext* ctx, const std::unordered_set<K>* s) {
        return JsConverter<std::unordered_set<K>&>::toJs(ctx, *s);
    }
};
template <typename K>
struct JsConverter<std::reference_wrapper<std::unordered_set<K>>> {
    static JSValue toJs(JSContext* ctx, std::reference_wrapper<std::unordered_set<K>> s) {
        return JsConverter<std::unordered_set<K>&>::toJs(ctx, s);
    }
};
template <typename K>
struct JsConverter<std::reference_wrapper<const std::unordered_set<K>>> {
    static JSValue toJs(JSContext* ctx, std::reference_wrapper<const std::unordered_set<K>> s) {
        return JsConverter<std::unordered_set<K>&>::toJs(ctx, s);
    }
};


class StringWrapper {
public:
    static void bind(ClassBinder<std::string>& cls)
    {
        // Used to create a new string object from a C++ string. or compare with js strings
        cls.method("valueOf", [ctx = cls.context()](std::string& s) -> JsValue {
            return JsValue::adopt(ctx, JsConverter<std::string>::toJs(ctx, s));
        });
        cls.method("toString", [ctx = cls.context()](std::string& s) -> JsValue {
            return JsValue::adopt(ctx, JsConverter<std::string>::toJs(ctx, s));
        });
    }

    static constexpr const char* className() { return "StdString"; }
};

template <>
struct JsConverter<std::string&> {
    static JSValue toJs(JSContext* ctx, const std::string& s) {
        // Auto-register on first use (once per JSContext, amortized O(1)).
        JSRuntime* rt = JS_GetRuntime(ctx);
        // Lazily allocate the JSClassID (global, once per type in the process).
        JSClassID cid = ClassRegistry::classId<std::string>(rt);
        // Quick check: if the prototype is already set, we're done.
        // JS_GetClassProto returns JS_NULL when the class was never installed.
        // This is O(1) — no atom lookup, no heap allocation.
        JSValue proto = JS_GetClassProto(ctx, cid);

        do {
            if (!JS_IsNull(proto)) {
                break;
            }

            // First time for this context — register the class and bind methods.
            // We use ClassBinder but call installInto() on a throwaway object
            // instead of install() (to avoid polluting the global namespace).
            // The constructor is intentionally NOT registered — wrapper objects
            // can only be created from C++.
            ClassBinder<std::string> binder(ctx, StringWrapper::className());
            StringWrapper::bind(binder);
            binder.install();
        } while (false);

        JSValue obj = JS_NewObjectProtoClass(ctx, proto, cid);
        JS_FreeValue(ctx, proto);

        auto* pd = ClassRegistry::makeBorrowed(const_cast<std::string*>(&s));
        JS_SetOpaque(obj, pd);
        return obj;
    }
};
template <>
struct JsConverter<const std::string&> {
    static JSValue toJs(JSContext* ctx, const std::string& s) {
        return JsConverter<std::string&>::toJs(ctx, s);
    }
};
template <>
struct JsConverter<std::string*> {
    static JSValue toJs(JSContext* ctx, const std::string* s) {
        return JsConverter<std::string&>::toJs(ctx, *s);
    }
};
template <>
struct JsConverter<const std::string*> {
    static JSValue toJs(JSContext* ctx, const std::string* s) {
        return JsConverter<std::string&>::toJs(ctx, *s);
    }
};
template <>
struct JsConverter<std::reference_wrapper<std::string>> {
    static JSValue toJs(JSContext* ctx, std::reference_wrapper<std::string> s) {
        return JsConverter<std::string&>::toJs(ctx, s);
    }
};
template <>
struct JsConverter<std::reference_wrapper<const std::string>> {
    static JSValue toJs(JSContext* ctx, std::reference_wrapper<const std::string> s) {
        return JsConverter<std::string&>::toJs(ctx, s);
    }
};

} // namespace qjsbind
