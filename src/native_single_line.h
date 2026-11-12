#pragma once
#include "native_text_fields.h"
#include "text_viewport.h"
#include <unordered_map>

namespace swml4::text_fields::single_line {
using detail::field;
struct String { const char* data; uint32_t length, capacity; };
using Update = void(*)(void*, uintptr_t, uintptr_t);
using Layout = void(*)(void*, uintptr_t, const String*);
using Draw = void(*)(void*, uintptr_t, uintptr_t, uintptr_t, float, float);
using Hit = uint32_t(*)(void*, const float*);
using Delete = void*(*)(void*, uint64_t);
using Glyph = const void*(*)(uintptr_t, uint32_t, int, int);
using Kerning = int32_t(*)(uintptr_t, uint32_t, uint32_t);
using Mesh = void(*)(void*, uintptr_t, const String*);
struct State {
    std::string source, rendered;
    TextViewport view;
    uintptr_t font = 0;
    float scale = 0, width = 0, height = 0;
    bool mesh_ready = false;
};
struct Bindings {
    Update original_update = nullptr;
    Layout original_layout = nullptr;
    Draw original_draw = nullptr;
    Hit original_hit = nullptr;
    Delete original_delete = nullptr;
    Glyph glyph = nullptr; Kerning kerning = nullptr; Mesh mesh = nullptr;
    uintptr_t table = 0;
    float font_scale = .8f;
    bool active = false;
    std::unordered_map<void*, State> states;
};
inline Bindings& bindings() { static Bindings b; return b; }
constexpr float padding = .015f, caret_width = .005f;
inline bool eligible(void* p) {
    auto& b = bindings();
    return b.active && field<uintptr_t>(p, 0) == b.table && !field<uint8_t>(p, 0x100) &&
        !field<uint8_t>(p, 0x101) && std::isfinite(field<float>(p, 0x10)) && field<float>(p, 0x10) > 2 * padding + caret_width &&
        std::isfinite(field<float>(p, 0x14)) && field<float>(p, 0x14) > .01f && field<uintptr_t>(p, 0x90);
}
inline uint32_t active_caret(void* p) {
    const auto start = field<uint32_t>(p, 0x30), selected = field<uint32_t>(p, 0x44), length = field<uint32_t>(p, 0xc0);
    return static_cast<uint32_t>(std::min<uint64_t>(length, uint64_t(start) + (field<uint8_t>(p, 0x41) ? 0 : selected)));
}
inline State* metrics(void* p) {
    auto& b = bindings();
    const auto& value = field<String>(p, 0xb8);
    if (!value.data || value.length >= value.capacity || value.length > 1024 * 1024) return nullptr;
    const auto font = field<uintptr_t>(p, 0x90);
    const auto count = field<uint32_t>(reinterpret_cast<void*>(font), 0x18);
    if (count > 65536) return nullptr;
    auto& state = b.states[p];
    const float scale = field<float>(p, 0xc8) * b.font_scale;
    if (!std::isfinite(scale) || scale <= 0 || scale > .01f) return nullptr;
    const std::string_view source(value.data, value.length);
    if (source != state.source || state.font != font || state.scale != scale || state.view.bytes != value.length) {
        state.source.assign(source);
        state.font = font; state.scale = scale;
        state.view.assign(source, [&](uint32_t previous, uint32_t code) {
            const auto* glyph = count ? b.glyph(font, code, 0, int(count) - 1) : nullptr;
            const float advance = glyph ? *reinterpret_cast<const int16_t*>(static_cast<const char*>(glyph) + 12) * scale : 0;
            return std::pair{advance, glyph ? b.kerning(font, previous, code) * scale : 0.f};
        });
        state.mesh_ready = false;
    }
    state.view.follow(active_caret(p), field<float>(p, 0x10) - 2 * padding - caret_width);
    return &state;
}
inline bool refresh(void* p, uintptr_t application) {
    auto* state = metrics(p);
    if (!state || !application) return false;
    const float width = field<float>(p, 0x10), height = field<float>(p, 0x14);
    const float inner = width - 2 * padding;
    const auto visible = state->view.visible();
    const bool native_reset = field<float>(p, 0x70) != 1 || field<uint32_t>(p, 0x74) != 3 ||
        field<float>(p, 0x78) != inner || field<float>(p, 0x7c) != height || field<float>(p, 0x88) != state->scale ||
        (!visible.empty() && !field<uintptr_t>(p, 0x98));
    if (!state->mesh_ready || native_reset || state->rendered != visible || state->width != width || state->height != height) {
        field<float>(p, 0x70) = 1;
        field<uint32_t>(p, 0x74) = 3;
        field<float>(p, 0x78) = inner;
        field<float>(p, 0x7c) = height;
        field<float>(p, 0x88) = state->scale;
        const String slice{visible.c_str(), static_cast<uint32_t>(visible.size()), static_cast<uint32_t>(visible.size() + 1)};
        bindings().mesh(static_cast<char*>(p) + 0x70, field<uintptr_t>(reinterpret_cast<void*>(application), 8), &slice);
        if (auto* mesh = field<void*>(p, 0x98)) {
            auto* vertices = field<TextVertex*>(mesh, 0x28);
            const auto count = field<uint32_t>(mesh, 0x4c), capacity = field<uint32_t>(mesh, 0x30);
            if (!vertices || count % 4 || count > visible.size() * 4 || uint64_t(count) * sizeof(TextVertex) > capacity) return false;
            for (uint32_t i = 0; i < count; i += 4)
                clip_text_quad(vertices + i, -inner * .5f, inner * .5f, -height * .5f + .002f, height * .5f - .002f);
        }
        state->width = width; state->height = height; state->rendered = visible; state->mesh_ready = true;
    }
    const auto start = std::min(field<uint32_t>(p, 0x30), field<uint32_t>(p, 0xc0));
    const auto end = static_cast<uint32_t>(std::min<uint64_t>(field<uint32_t>(p, 0xc0), uint64_t(start) + field<uint32_t>(p, 0x44)));
    const float budget = inner - caret_width;
    const float left = std::clamp(state->view.position(state->view.index(start)), 0.f, budget);
    const float right = std::clamp(state->view.position(state->view.index(end)), left, budget);
    field<float>(p, 0x3c) = -width * .5f + padding + left;
    field<float>(p, 0x48) = right - left;
    return true;
}
inline void layout(void* p, uintptr_t application, const String* source) {
    if (!eligible(p) || !refresh(p, application)) bindings().original_layout(p, application, source);
}
inline void update(void* p, uintptr_t traversal, uintptr_t application) {
    if (!eligible(p) || !refresh(p, application)) { bindings().original_update(p, traversal, application); return; }
    if (++field<uint32_t>(p, 0x34) == 32) { field<uint8_t>(p, 0x38) = !field<uint8_t>(p, 0x38); field<uint32_t>(p, 0x34) = 0; }
    if (field<uint8_t>(p, 0x59) && ++field<uint32_t>(p, 0x54) > 30) { field<uint8_t>(p, 0x59) = 0; field<uint32_t>(p, 0x54) = 0; }
}
inline void draw(void* p, uintptr_t traversal, uintptr_t application, uintptr_t pass, float x, float y) {
    const bool ours = eligible(p);
    const bool ready = !ours || refresh(p, application);
    const auto mesh = field<uintptr_t>(p, 0x98);
    const auto selected = field<uint32_t>(p, 0x44);
    const auto focused = field<uint8_t>(p, 0xe8);
    if (!ready) { field<uintptr_t>(p, 0x98) = 0; field<uint32_t>(p, 0x44) = 0; field<uint8_t>(p, 0xe8) = 0; }
    bindings().original_draw(p, traversal, application, pass, x, y);
    if (!ready) { field<uintptr_t>(p, 0x98) = mesh; field<uint32_t>(p, 0x44) = selected; field<uint8_t>(p, 0xe8) = focused; }
}
inline uint32_t hit(void* p, const float* position) {
    if (!eligible(p)) return bindings().original_hit(p, position);
    auto* state = metrics(p);
    if (!state) return bindings().original_hit(p, position);
    if (position[0] < padding && state->view.first) --state->view.first;
    else if (position[0] > field<float>(p, 0x10) - padding && state->view.first < state->view.glyphs.size()) ++state->view.first;
    state->view.follow(state->view.byte(state->view.first), field<float>(p, 0x10) - 2 * padding - caret_width);
    field<uint32_t>(p, 0x34) = 0; field<uint8_t>(p, 0x38) = 1;
    return state->view.hit(position[0] - padding);
}
inline void* destroy(void* p, uint64_t flags) {
    bindings().states.erase(p);
    return bindings().original_delete(p, flags);
}
inline bool prepare(float font_scale, std::map<std::string, uintptr_t> functions) {
    auto& b = bindings();
    b.table = native_find_vtable("editbox_content@c_ui_element_editbox");
    if (!b.table) return false;
    const auto* table = reinterpret_cast<const uintptr_t*>(b.table);
    if (table[0] != functions["simple_delete"] || table[1] != functions["simple_init"] || table[3] != functions["simple_update"] ||
        table[4] != functions["simple_draw"] || table[28] != functions["simple_layout"]) return false;
    b.font_scale = font_scale;
    b.glyph = reinterpret_cast<Glyph>(functions["glyph"]);
    b.kerning = reinterpret_cast<Kerning>(functions["kerning"]);
    b.mesh = reinterpret_cast<Mesh>(functions["simple_mesh"]);
    return hook_address(functions["simple_layout"], reinterpret_cast<void*>(layout), reinterpret_cast<void**>(&b.original_layout)) &&
        hook_address(functions["simple_update"], reinterpret_cast<void*>(update), reinterpret_cast<void**>(&b.original_update)) &&
        hook_address(functions["simple_draw"], reinterpret_cast<void*>(draw), reinterpret_cast<void**>(&b.original_draw)) &&
        hook_address(functions["simple_hit"], reinterpret_cast<void*>(hit), reinterpret_cast<void**>(&b.original_hit)) &&
        hook_address(functions["simple_delete"], reinterpret_cast<void*>(destroy), reinterpret_cast<void**>(&b.original_delete));
}
}
