set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(D3D12 REQUIRED)

set(HLSL_COMPILE_DEFNS "")
if (REPORT_RAY_STATS)
	set(HLSL_COMPILE_DEFNS "REPORT_RAY_STATS=1")
endif()

add_dxil_embed_library(dxr_shaders render_dxr.hlsl util.hlsl
	disney_bsdf.hlsl lcg_rng.hlsl
	COMPILE_OPTIONS -O3
	COMPILE_DEFINITIONS
        ${HLSL_COMPILE_DEFNS}
	INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_LIST_DIR}
        ${PROJECT_SOURCE_DIR})

add_library(dxr
    render_dxr.cpp
    dxdisplay.cpp
    dx12_utils.cpp
    dxr_utils.cpp
    imgui_impl_dx12.cpp)

set_target_properties(dxr PROPERTIES
	CXX_STANDARD 14
	CXX_STANDARD_REQUIRED ON)

if (REPORT_RAY_STATS)
	target_compile_options(dxr PUBLIC
		-DREPORT_RAY_STATS=1)
endif()

target_include_directories(dxr PUBLIC
	$<BUILD_INTERFACE:${D3D12_INCLUDE_DIRS}>)

target_link_libraries(dxr PUBLIC
	dxr_shaders util ${D3D12_LIBRARIES})

