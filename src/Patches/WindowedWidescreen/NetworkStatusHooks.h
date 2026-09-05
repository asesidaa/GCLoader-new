#pragma once
#include "Patches/WindowedWidescreen/WidescreenRuntime.h"

namespace gc::windowed_widescreen::detail {
struct NetworkStatusOriginals final {
    native::MovieClipAccept network_status_movie_clip_accept{};
    native::ShapeDrawVisit network_status_shape_draw_visit{};
};
extern NetworkStatusOriginals g_network_originals;
extern thread_local std::uint32_t g_network_status_native_scope_depth;

void __fastcall NetworkStatusShapeDrawVisitDetour(
    void* const visitor,
    void*,
    void* const definition) noexcept;
int __fastcall NetworkStatusMovieClipAcceptDetour(
    void* const movie_clip,
    void*,
    void* const visitor) noexcept;
}
