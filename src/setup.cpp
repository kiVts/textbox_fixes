#include "sdk_v4.hpp"
#include "native_single_line.h"

namespace {
bool setup() {
    if (!swml4::api()) return false;
    swml4::text_fields::Options options;
    options.font_scale = static_cast<float>(swml4::number("font_scale", .8));
    options.box_height = static_cast<float>(swml4::number("box_height", .21));
    std::map<std::string, uintptr_t> functions;
    if (!swml4::text_fields::valid(options) || !swml4::text_fields::detail::resolve_contracts(functions) ||
        !swml4::text_fields::single_line::prepare(options.font_scale, functions) ||
        !swml4::text_fields::install(options, functions)) {
        mod_status("unavailable: native scrolling text-field contract changed");
        return false;
    }
    swml4::text_fields::single_line::bindings().log = swlog;
    swml4::text_fields::detail::bindings().log = swlog;
    swml4::text_fields::single_line::bindings().active = true;
    swlog("text fields 0.1.7: full-width string, number and expression properties; base/derived single-line and all editable advanced fields; font %.0f%%, height %.3f", options.font_scale * 100, options.box_height);
    mod_status("scrolling text fields (restart after settings changes)");
    return true;
}
ModSetup registration{setup};
}
