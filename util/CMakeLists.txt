add_subdirectory(display)

add_library(util
    arcball_camera.cpp
    util.cpp
    material.cpp
    mesh.cpp
    scene.cpp
    buffer_view.cpp
    gltf_types.cpp
    flatten_gltf.cpp
    file_mapping.cpp
    xatlas.cpp)

set_target_properties(util PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED ON)

target_include_directories(util PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/parallel_hashmap>)

target_link_libraries(util PUBLIC
    imgui
    glm
    SDL2::SDL2)

