if(NOT DEFINED IMGUI_HEADER OR NOT EXISTS "${IMGUI_HEADER}")
    message(FATAL_ERROR "GenerateTracyImGuiShim: missing IMGUI_HEADER='${IMGUI_HEADER}'")
endif()
if(NOT DEFINED OUTPUT_HEADER)
    message(FATAL_ERROR "GenerateTracyImGuiShim: missing OUTPUT_HEADER")
endif()

file(READ "${IMGUI_HEADER}" _imgui_content)

set(_add_polyline_anchor "IMGUI_API void  AddPolyline(const ImVec2* points, int num_points, ImU32 col, ImDrawFlags flags, float thickness);")
string(FIND "${_imgui_content}" "${_add_polyline_anchor}" _anchor_pos)
if(_anchor_pos EQUAL -1)
    message(FATAL_ERROR "GenerateTracyImGuiShim: failed to locate AddPolyline anchor in '${IMGUI_HEADER}'")
endif()

set(_drawlist_shims "    inline    void  AddPolyline(const ImVec2* points, int num_points, ImU32 col, float thickness) { AddPolyline(points, num_points, col, 0, thickness); }
    inline    void  AddLineH(float x0, float x1, float y, ImU32 col, float thickness = 1.0f) { AddLine(ImVec2(x0, y), ImVec2(x1, y), col, thickness); }
    inline    void  AddLineV(float x, float y0, float y1, ImU32 col, float thickness = 1.0f) { AddLine(ImVec2(x, y0), ImVec2(x, y1), col, thickness); }")

string(REPLACE
    "${_add_polyline_anchor}"
    "${_add_polyline_anchor}\n${_drawlist_shims}"
    _imgui_content
    "${_imgui_content}"
)

string(APPEND _imgui_content "\n#ifndef IMGUI_DISABLE\nnamespace ImGui {\n    inline void SetNextWindowFlags(ImGuiWindowFlags flags, bool enabled) { (void)flags; (void)enabled; }\n}\n#endif\n")

get_filename_component(_out_dir "${OUTPUT_HEADER}" DIRECTORY)
file(MAKE_DIRECTORY "${_out_dir}")
file(WRITE "${OUTPUT_HEADER}" "${_imgui_content}")

if(DEFINED IMGUI_INTERNAL_HEADER AND DEFINED OUTPUT_INTERNAL_HEADER)
    if(NOT EXISTS "${IMGUI_INTERNAL_HEADER}")
        message(FATAL_ERROR "GenerateTracyImGuiShim: missing IMGUI_INTERNAL_HEADER='${IMGUI_INTERNAL_HEADER}'")
    endif()
    get_filename_component(_internal_out_dir "${OUTPUT_INTERNAL_HEADER}" DIRECTORY)
    file(MAKE_DIRECTORY "${_internal_out_dir}")
    file(WRITE "${OUTPUT_INTERNAL_HEADER}" "#pragma once\n#include \"imgui.h\"\n#include \"${IMGUI_INTERNAL_HEADER}\"\n")
endif()
