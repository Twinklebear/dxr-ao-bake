cmake_minimum_required(VERSION 3.5)
project(dxr_ao_bake)

if (NOT WIN32)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
endif()

add_definitions(-DNOMINMAX -DGLM_ENABLE_EXPERIMENTAL -DSDL_MAIN_HANDLED
    -DWIN32_LEAN_AND_MEAN)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(Threads REQUIRED)
find_package(glm REQUIRED)
find_package(SDL2 REQUIRED)

add_subdirectory(imgui)
add_subdirectory(util)
add_subdirectory(dxr)

add_dxil_embed_library(render_ao_map_vs
    render_ao_map.hlsl
    COMPILE_OPTIONS -O3 -T vs_6_3 -E vsmain)

add_dxil_embed_library(render_ao_map_fs
    render_ao_map.hlsl
    COMPILE_OPTIONS -O3 -T ps_6_3 -E fsmain)

add_executable(dxr_ao_bake main.cpp)

set_target_properties(dxr_ao_bake PROPERTIES
	CXX_STANDARD 14
	CXX_STANDARD_REQUIRED ON)

target_link_libraries(dxr_ao_bake PUBLIC
    util
    display
    dxr
    render_ao_map_vs
    render_ao_map_fs)

