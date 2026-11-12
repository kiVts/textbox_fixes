#pragma once
#include "hooks.h"
#include "resolve.h"
#include "native_text_field_signatures.h"
#include "native_ui_focus.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>

namespace swml4::text_fields {
struct Options {
    float font_scale = .8f;
    float box_height = .21f;
};
inline bool valid(Options o) {
    return std::isfinite(o.font_scale) && o.font_scale >= .65f && o.font_scale <= 1.f &&
        std::isfinite(o.box_height) && o.box_height >= .125f && o.box_height <= .8f;
}
namespace detail {
template<class T> inline T& field(void* p, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(p) + offset);
}
using Init = void(*)(void*, uintptr_t);
using Editor = void*(*)(void*, float, float, float, float, uint32_t, uint8_t, uintptr_t);
using Text = void*(*)(void*, float, float, float, float, uint32_t, uint8_t, uint8_t);
using Key = uint64_t(*)(void*, uintptr_t, uintptr_t, uintptr_t, uint32_t, uint32_t);
using Repeat = uint64_t(*)(void*, uintptr_t, uintptr_t, uint32_t);
using TextDelete = void*(*)(void*, uint64_t);
struct Bindings {
    Init original_row = nullptr, multiline_init = nullptr;
    Editor original_editor = nullptr;
    Text original_text = nullptr;
    Key original_key = nullptr;
    Repeat original_repeat = nullptr;
    TextDelete original_text_delete = nullptr;
    std::set<void*> owned_text;
    uintptr_t row_table = 0, multiline_table = 0;
    Options options;
    bool active = false;
};
inline Bindings& bindings() { static Bindings b; return b; }
struct Request {
    void* row = nullptr;
    bool editor_pending = true, text_pending = false;
};
inline thread_local Request* current_request = nullptr;
struct Scope {
    Request* previous;
    explicit Scope(Request& request) : previous(current_request) { current_request = &request; }
    ~Scope() { current_request = previous; }
};
inline void* text_ctor(void* object, float x, float y, float width, float height,
                       uint32_t anchor, uint8_t syntax, uint8_t readonly) {
    auto& b = bindings();
    auto* request = current_request;
    const bool ours = b.active && request && request->text_pending;
    if (ours) request->text_pending = false;
    void* result = b.original_text(object, x, y, width, height, anchor, syntax, readonly);
    if (ours) {
        b.owned_text.insert(result);
        field<float>(result, 0x70) = .0005f * b.options.font_scale;
        field<float>(result, 0x74) = 1.f;
        auto* value = field<void*>(request->row, 0x68);
        if (value) field<uint32_t>(result, 0x1f8) = std::max(4096u, field<uint32_t>(value, 8));
    }
    return result;
}
inline bool typing(void* object, uintptr_t application) {
    auto& b = bindings();
    return b.active && b.owned_text.contains(object) && native_ui_keyboard_focus(application) == object &&
        field<uint8_t>(object, 0x1c) && field<uint8_t>(object, 0x1db) && !field<uint8_t>(object, 0x1f0);
}
inline uint64_t key(void* object, uintptr_t input, uintptr_t application, uintptr_t events, uint32_t code, uint32_t state) {
    const bool captured = typing(object, application);
    const auto result = bindings().original_key(object, input, application, events, code, state);
    return captured ? (result & ~uint64_t(255)) | 1 : result;
}
inline uint64_t repeat(void* object, uintptr_t application, uintptr_t events, uint32_t code) {
    const bool captured = typing(object, application);
    const auto result = bindings().original_repeat(object, application, events, code);
    return captured ? (result & ~uint64_t(255)) | 1 : result;
}
inline void* text_delete(void* object, uint64_t flags) {
    bindings().owned_text.erase(object);
    return bindings().original_text_delete(object, flags);
}
inline void* editor_ctor(void* object, float x, float y, float width, float height,
                         uint32_t anchor, uint8_t syntax, uintptr_t scratch) {
    auto& b = bindings();
    auto* request = current_request;
    const bool ours = b.active && request && request->editor_pending;
    if (ours) {
        request->editor_pending = false;
        request->text_pending = true;
        auto* label = field<void*>(request->row, 0x80);
        const float label_height = std::max(.075f, field<float>(label, 0x14));
        x = 0; y = -label_height - .025f;
        width = field<float>(request->row, 0x10);
        height = b.options.box_height;
        anchor = 0;
    }
    return b.original_editor(object, x, y, width, height, anchor, syntax, scratch);
}
inline void row_init(void* row, uintptr_t application) {
    auto& b = bindings();
    const auto table = field<uintptr_t>(row, 0);
    const bool multiline = table == b.multiline_table;
    if (!b.active || (table != b.row_table && !multiline) ||
        !std::isfinite(field<float>(row, 0x10)) || field<float>(row, 0x10) < .2f) {
        (multiline ? b.multiline_init : b.original_row)(row, application);
        return;
    }
    field<uintptr_t>(row, 0) = b.multiline_table;
    field<float>(row, 0x58) = field<float>(row, 0x10);
    Request request{row};
    Scope scope(request);
    b.multiline_init(row, application);
    auto* label = field<void*>(row, 0x80);
    field<float>(label, 8) = 0;
    field<float>(label, 12) = 0;
    field<uint32_t>(label, 0x18) = 0;
    field<float>(row, 0x14) = std::max(.075f, field<float>(label, 0x14)) + .025f + b.options.box_height;
}
inline bool matches(uintptr_t address, const char* pattern) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(address);
    auto hex = [](char c) { return c <= '9' ? c - '0' : c - 'A' + 10; };
    while (*pattern) {
        if (*pattern == ' ') { ++pattern; continue; }
        if (*pattern != '?' && *bytes != ((hex(pattern[0]) << 4) | hex(pattern[1]))) return false;
        ++bytes; pattern += 2;
    }
    return true;
}
inline bool resolve_contracts(std::map<std::string, uintptr_t>& functions) {
    for (const auto& sig : kNativeTextFieldSignatures) {
        const auto address = native_find_pattern(sig.scan);
        if (!address) return false;
        const auto bytes = (strlen(sig.full) + 1) / 3;
        const auto limit = address + bytes;
        auto cursor = bytes <= 512 ? limit : address;
        for (unsigned fragment = 0; cursor < limit && fragment < 16; ++fragment) {
            uintptr_t begin = 0, end = 0;
            if (!native_function_bounds(cursor, &begin, &end) || begin != cursor || end <= cursor) return false;
            cursor = end;
        }
        if (cursor < limit || !matches(address, sig.full)) return false;
        functions[sig.name] = address;
    }
    for (const auto& call : kNativeTextFieldCalls) {
        const auto at = functions[call.caller] + call.offset;
        int32_t displacement = 0;
        memcpy(&displacement, reinterpret_cast<const void*>(at + 1), sizeof(displacement));
        if (*reinterpret_cast<const uint8_t*>(at) != 0xe8 || at + 5 + displacement != functions[call.callee]) return false;
    }
    return true;
}
}

inline bool install(Options options = {}, std::map<std::string, uintptr_t> functions = {}) {
    using namespace detail;
    auto& b = bindings();
    if (b.active || !valid(options)) return false;
    if (functions.empty() && !resolve_contracts(functions)) return false;
    b.row_table = native_find_vtable("c_ui_properties_element_text_string");
    b.multiline_table = native_find_vtable("c_ui_properties_element_text_string_multiline");
    if (!b.row_table || !b.multiline_table) return false;
    const auto* row = reinterpret_cast<const uintptr_t*>(b.row_table);
    const auto* multiline = reinterpret_cast<const uintptr_t*>(b.multiline_table);
    if (row[0] != functions["row_delete"] || row[1] != functions["row_init"] ||
        row[37] != functions["row_write"] || multiline[0] != row[0] ||
        multiline[1] != functions["multiline_init"] || multiline[37] != functions["multiline_write"]) return false;
    for (size_t slot = 2; slot < 37; ++slot) if (row[slot] != multiline[slot]) return false;
    const auto advanced = native_find_vtable("c_ui_element_editbox_adv");
    const auto text = native_find_vtable("c_ui_element_editbox_adv_text");
    if (!advanced || !text || reinterpret_cast<const uintptr_t*>(advanced)[1] != functions["editor_init"] ||
        reinterpret_cast<const uintptr_t*>(advanced)[3] != functions["editor_update"] ||
        reinterpret_cast<const uintptr_t*>(text)[1] != functions["text_init"] ||
        reinterpret_cast<const uintptr_t*>(text)[0] != functions["text_delete"] ||
        reinterpret_cast<const uintptr_t*>(text)[20] != functions["text_key"] ||
        reinterpret_cast<const uintptr_t*>(text)[23] != functions["text_repeat"]) return false;
    b.options = options;
    if (!hook_address(functions["editor_ctor"], reinterpret_cast<void*>(editor_ctor), reinterpret_cast<void**>(&b.original_editor)) ||
        !hook_address(functions["text_ctor"], reinterpret_cast<void*>(text_ctor), reinterpret_cast<void**>(&b.original_text)) ||
        !hook_address(functions["text_key"], reinterpret_cast<void*>(key), reinterpret_cast<void**>(&b.original_key)) ||
        !hook_address(functions["text_repeat"], reinterpret_cast<void*>(repeat), reinterpret_cast<void**>(&b.original_repeat)) ||
        !hook_address(functions["text_delete"], reinterpret_cast<void*>(text_delete), reinterpret_cast<void**>(&b.original_text_delete)) ||
        !hook_address(functions["multiline_init"], reinterpret_cast<void*>(row_init), reinterpret_cast<void**>(&b.multiline_init)) ||
        !hook_address(functions["row_init"], reinterpret_cast<void*>(row_init), reinterpret_cast<void**>(&b.original_row))) return false;
    b.active = true;
    return true;
}
}
