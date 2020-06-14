set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(D3D12 REQUIRED)

add_library(dxr
    dxdisplay.cpp
    dx12_utils.cpp
    dxr_utils.cpp
    imgui_impl_dx12.cpp)

set_target_properties(dxr PROPERTIES
	CXX_STANDARD 14
	CXX_STANDARD_REQUIRED ON)

target_include_directories(dxr PUBLIC
	$<BUILD_INTERFACE:${D3D12_INCLUDE_DIRS}>)

target_link_libraries(dxr PUBLIC
	util ${D3D12_LIBRARIES})

