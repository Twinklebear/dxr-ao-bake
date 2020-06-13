#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <vector>
#include <SDL.h>
#include "arcball_camera.h"
#include "dxr/dxdisplay.h"
#include "dxr/render_dxr.h"
#include "imgui.h"
#include "scene.h"
#include "stb_image_write.h"
#include "tiny_obj_loader.h"
#include "util.h"
#include "util/display/display.h"
#include "util/display/gldisplay.h"
#include "util/display/imgui_impl_sdl.h"
#include "util/xatlas.h"

const std::string USAGE = "Usage: <backend> <obj/gltf_file>\n";

int win_width = 1280;
int win_height = 720;

void run_app(const std::vector<std::string> &args, SDL_Window *window, Display *display);

glm::vec2 transform_mouse(glm::vec2 in)
{
    return glm::vec2(in.x * 2.f / win_width - 1.f, 1.f - 2.f * in.y / win_height);
}

int main(int argc, const char **argv)
{
    const std::vector<std::string> args(argv, argv + argc);
    auto fnd_help = std::find_if(args.begin(), args.end(), [](const std::string &a) {
        return a == "-h" || a == "--help";
    });

    if (argc < 2 || fnd_help != args.end()) {
        std::cout << USAGE;
        return 1;
    }

    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
        std::cerr << "Failed to init SDL: " << SDL_GetError() << "\n";
        return -1;
    }

    // Determine which display frontend we should use
    uint32_t window_flags = SDL_WINDOW_RESIZABLE;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-img") {
            win_width = std::stoi(args[++i]);
            win_height = std::stoi(args[++i]);
            continue;
        }
    }

    SDL_Window *window = SDL_CreateWindow("ChameleonRT",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          win_width,
                                          win_height,
                                          window_flags);
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    {
        std::unique_ptr<Display> display = std::make_unique<DXDisplay>(window);

        run_app(args, window, display.get());
    }

    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

void run_app(const std::vector<std::string> &args, SDL_Window *window, Display *display)
{
    ImGuiIO &io = ImGui::GetIO();

    DXDisplay *dx_display = dynamic_cast<DXDisplay *>(display);

    std::unique_ptr<RenderDXR> renderer =
        std::make_unique<RenderDXR>(dx_display->device, true);

    std::string scene_file = args[1];
    /*
    for (size_t i = 1; i < args.size(); ++i) {
        scene_file = args[i];
        canonicalize_path(scene_file);
    }
    */
    if (!renderer) {
        std::cout << "Error: No renderer backend or invalid backend name specified\n" << USAGE;
        std::exit(1);
    }
    if (scene_file.empty()) {
        std::cout << "Error: No model file specified\n" << USAGE;
        std::exit(1);
    }

    display->resize(win_width, win_height);
    renderer->initialize(win_width, win_height);

    glm::uvec2 atlas_size;
    std::string scene_info;
    {
        Scene scene(scene_file);

        std::stringstream ss;
        ss << "Scene '" << scene_file << "':\n"
           << "# Unique Triangles: " << pretty_print_count(scene.unique_tris()) << "\n"
           << "# Total Triangles: " << pretty_print_count(scene.total_tris()) << "\n"
           << "# Geometries: " << scene.num_geometries() << "\n"
           << "# Meshes: " << scene.meshes.size() << "\n"
           << "# Instances: " << scene.instances.size() << "\n"
           << "# Materials: " << scene.materials.size() << "\n"
           << "# Textures: " << scene.textures.size() << "\n"
           << "# Lights: " << scene.lights.size() << "\n"
           << "# Cameras: " << scene.cameras.size();

        scene_info = ss.str();
        std::cout << scene_info << "\n";

        auto *atlas = xatlas::Create();
        const size_t total_geometries = scene.num_geometries();
        for (const auto &m : scene.meshes) {
            for (const auto &g : m.geometries) {
                xatlas::MeshDecl mesh;
                mesh.vertexCount = g.vertices.size();
                mesh.vertexPositionData = g.vertices.data();
                mesh.vertexPositionStride = sizeof(glm::vec3);

                mesh.indexCount = g.indices.size() * 3;
                mesh.indexData = g.indices.data();
                mesh.indexFormat = xatlas::IndexFormat::UInt32;

                if (!g.uvs.empty()) {
                    mesh.vertexUvData = g.uvs.data();
                    mesh.vertexUvStride = sizeof(glm::vec2);
                }

                if (!g.normals.empty()) {
                    mesh.vertexNormalData = g.normals.data();
                    mesh.vertexNormalStride = sizeof(glm::vec3);
                }

                auto err = xatlas::AddMesh(atlas, mesh, total_geometries);
                if (err != xatlas::AddMeshError::Success) {
                    xatlas::Destroy(atlas);
                    std::cout << "Error adding geometry to atlas: "
                              << xatlas::StringForEnum(err) << "\n";
                    throw std::runtime_error("Error adding geometry to atlas");
                }
            }
        }

        std::cout << "Generating atlas\n";
        xatlas::Generate(atlas);
        std::cout << "Atlas generated:\n"
                  << "  # of charts: " << atlas->chartCount << "\n"
                  << "  # of atlases: " << atlas->atlasCount << "\n"
                  << "  Resolution: " << atlas->width << "x" << atlas->height << "\n";

        atlas_size = glm::uvec2(atlas->width, atlas->height);

        // Replace the mesh data with the atlas mesh data
        size_t mesh_id = 0;
        for (auto &m : scene.meshes) {
            for (auto &g : m.geometries) {
                const auto &mesh = atlas->meshes[mesh_id++];
                std::vector<glm::vec3> atlas_verts;
                std::vector<glm::vec2> atlas_uvs;
                atlas_verts.reserve(mesh.vertexCount);
                atlas_uvs.reserve(mesh.vertexCount);

                std::vector<glm::vec3> atlas_normals;
                if (!g.normals.empty()) {
                    atlas_normals.reserve(mesh.vertexCount);
                }
                for (size_t i = 0; i < mesh.vertexCount; ++i) {
                    const auto &vert_indices = mesh.vertexArray[i];
                    atlas_verts.push_back(g.vertices[vert_indices.xref]);

                    if (!g.normals.empty()) {
                        atlas_normals.push_back(g.normals[vert_indices.xref]);
                    }
                    atlas_uvs.push_back(glm::vec2(vert_indices.uv[0] / atlas_size.x,
                                                  vert_indices.uv[1] / atlas_size.y));
                }

                std::vector<glm::uvec3> atlas_indices;
                atlas_indices.reserve(mesh.indexCount / 3);
                for (size_t i = 0; i < mesh.indexCount / 3; ++i) {
                    atlas_indices.push_back(glm::uvec3(mesh.indexArray[i * 3],
                                                       mesh.indexArray[i * 3 + 1],
                                                       mesh.indexArray[i * 3 + 2]));
                }

                g.vertices = atlas_verts;
                g.normals = atlas_normals;
                g.uvs = atlas_uvs;
                g.indices = atlas_indices;
            }
        }
        xatlas::Destroy(atlas);

        renderer->set_scene(scene);
    }

    // TODO LATER: 2D panning controls for the atlas so we don't need the window dims to match
    // it
    win_width = atlas_size.x;
    win_height = atlas_size.y;
    SDL_SetWindowSize(window, win_width, win_height);

    display->resize(win_width, win_height);
    renderer->initialize(win_width, win_height);

    glm::vec3 eye(0, 0, 5);
    glm::vec3 center(0);
    glm::vec3 up(0, 1, 0);
    ArcballCamera camera(eye, center, up);

    const std::string rt_backend = renderer->name();
    const std::string cpu_brand = get_cpu_brand();
    const std::string gpu_brand = display->gpu_brand();
    const std::string image_output = "chameleonrt.png";
    const std::string display_frontend = display->name();

    size_t frame_id = 0;
    float render_time = 0.f;
    float rays_per_second = 0.f;
    glm::vec2 prev_mouse(-2.f);
    bool done = false;
    bool camera_changed = true;
    bool save_image = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                done = true;
            }
            if (!io.WantCaptureKeyboard && event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    done = true;
                }
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window)) {
                done = true;
            }
            if (!io.WantCaptureMouse) {
                if (event.type == SDL_MOUSEMOTION) {
                    const glm::vec2 cur_mouse =
                        transform_mouse(glm::vec2(event.motion.x, event.motion.y));
                    if (prev_mouse != glm::vec2(-2.f)) {
                        if (event.motion.state & SDL_BUTTON_LMASK) {
                            camera.rotate(prev_mouse, cur_mouse);
                            camera_changed = true;
                        } else if (event.motion.state & SDL_BUTTON_RMASK) {
                            camera.pan(cur_mouse - prev_mouse);
                            camera_changed = true;
                        }
                    }
                    prev_mouse = cur_mouse;
                } else if (event.type == SDL_MOUSEWHEEL) {
                    camera.zoom(event.wheel.y * 0.1);
                    camera_changed = true;
                }
            }
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_RESIZED) {
                frame_id = 0;
                win_width = event.window.data1;
                win_height = event.window.data2;
                io.DisplaySize.x = win_width;
                io.DisplaySize.y = win_height;

                display->resize(win_width, win_height);
            }
        }

        if (camera_changed) {
            frame_id = 0;
        }

        const bool need_readback = save_image;
        RenderStats stats = renderer->render(
            camera.eye(), camera.dir(), camera.up(), 45.f, camera_changed, need_readback);

        ++frame_id;
        camera_changed = false;

        if (save_image) {
            save_image = false;
            // TODO
            /*
            std::cout << "Image saved to " << image_output << "\n";
            stbi_write_png(image_output.c_str(),
                           win_width,
                           win_height,
                           4,
                           renderer->img.data(),
                           4 * win_width);
                           */
        }

        if (frame_id == 1) {
            render_time = stats.render_time;
            rays_per_second = stats.rays_per_second;
        } else {
            render_time += stats.render_time;
            rays_per_second += stats.rays_per_second;
        }

        display->new_frame();

        ImGui_ImplSDL2_NewFrame(window);
        ImGui::NewFrame();

        ImGui::Begin("Render Info");
        ImGui::Text("Render Time: %.3f ms/frame (%.1f FPS)",
                    render_time / frame_id,
                    1000.f / (render_time / frame_id));

        if (stats.rays_per_second > 0) {
            const std::string rays_per_sec = pretty_print_count(rays_per_second / frame_id);
            ImGui::Text("Rays per-second: %sRay/s", rays_per_sec.c_str());
        }

        ImGui::Text("Total Application Time: %.3f ms/frame (%.1f FPS)",
                    1000.0f / ImGui::GetIO().Framerate,
                    ImGui::GetIO().Framerate);
        ImGui::Text("RT Backend: %s", rt_backend.c_str());
        ImGui::Text("CPU: %s", cpu_brand.c_str());
        ImGui::Text("GPU: %s", gpu_brand.c_str());
        ImGui::Text("Accumulated Frames: %llu", frame_id);
        ImGui::Text("Display Frontend: %s", display_frontend.c_str());
        ImGui::Text("%s", scene_info.c_str());

        if (ImGui::Button("Save Image")) {
            save_image = true;
        }

        ImGui::End();
        ImGui::Render();

        RenderDXR *render_dx = reinterpret_cast<RenderDXR *>(renderer.get());
        dx_display->display_native(render_dx->render_target);
    }
}
