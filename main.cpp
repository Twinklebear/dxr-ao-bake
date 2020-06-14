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

#include "render_ao_map_fs_embedded_dxil.h"
#include "render_ao_map_vs_embedded_dxil.h"

const std::string USAGE = "Usage: <backend> <obj/gltf_file>\n";

int win_width = 512;
int win_height = 512;

void run_app(const std::vector<std::string> &args, SDL_Window *window, DXDisplay *display);

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

    SDL_Window *window = SDL_CreateWindow("DXR AO Baking",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          win_width,
                                          win_height,
                                          window_flags);
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    {
        std::unique_ptr<DXDisplay> display = std::make_unique<DXDisplay>(window);

        run_app(args, window, display.get());
    }

    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

void run_app(const std::vector<std::string> &args, SDL_Window *window, DXDisplay *display)
{
    ImGuiIO &io = ImGui::GetIO();

    std::unique_ptr<RenderDXR> renderer = std::make_unique<RenderDXR>(display->device, true);

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
                } else {
                    std::cout << "Normals are required on all objects\n";
                    throw std::runtime_error("Normals are required on all objects");
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
    // TODO: No longer need this init part for the ray tracer, but more there later
    renderer->initialize(win_width, win_height);

    D3D12_CLEAR_VALUE clear_value;
    clear_value.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    std::memset(clear_value.Color, 0, sizeof(clear_value.Color));
    clear_value.Color[3] = 1.f;

    auto ao_image = dxr::Texture2D::default(display->device.Get(),
                                            atlas_size,
                                            D3D12_RESOURCE_STATE_RENDER_TARGET,
                                            DXGI_FORMAT_R8G8B8A8_UNORM,
                                            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                            &clear_value);
    using Microsoft::WRL::ComPtr;

    // Make a descriptor heap
    ComPtr<ID3D12DescriptorHeap> rtv_heap;
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {0};
    rtv_heap_desc.NumDescriptors = 1;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    CHECK_ERR(display->device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap)));

    // Create render target descriptors heap for our AO baked
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    display->device->CreateRenderTargetView(ao_image.get(), nullptr, rtv_handle);

    // Make an empty root signature
    // TODO: This will take the TLAS later
    ComPtr<ID3D12RootSignature> root_signature;
    {
        D3D12_ROOT_SIGNATURE_DESC desc = {0};
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        CHECK_ERR(D3D12SerializeRootSignature(
            &desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
        CHECK_ERR(display->device->CreateRootSignature(0,
                                                       signature->GetBufferPointer(),
                                                       signature->GetBufferSize(),
                                                       IID_PPV_ARGS(&root_signature)));
    }

    ComPtr<ID3D12PipelineState> pipeline_state;
    {
        // Create the graphics pipeline state description
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {0};

        // Specify the vertex data layout
        std::array<D3D12_INPUT_ELEMENT_DESC, 3> vertex_layout = {
            D3D12_INPUT_ELEMENT_DESC{"POSITION",
                                     0,
                                     DXGI_FORMAT_R32G32B32_FLOAT,
                                     0,
                                     0,
                                     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                                     0},
            D3D12_INPUT_ELEMENT_DESC{"NORMAL",
                                     0,
                                     DXGI_FORMAT_R32G32B32_FLOAT,
                                     1,
                                     0,
                                     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                                     0},
            D3D12_INPUT_ELEMENT_DESC{"TEXCOORD",
                                     0,
                                     DXGI_FORMAT_R32G32_FLOAT,
                                     2,
                                     0,
                                     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                                     0}};

        desc.pRootSignature = root_signature.Get();

        desc.VS.pShaderBytecode = render_ao_map_vs_dxil;
        desc.VS.BytecodeLength = sizeof(render_ao_map_vs_dxil);
        desc.PS.pShaderBytecode = render_ao_map_fs_dxil;
        desc.PS.BytecodeLength = sizeof(render_ao_map_fs_dxil);

        desc.BlendState.AlphaToCoverageEnable = FALSE;
        desc.BlendState.IndependentBlendEnable = FALSE;
        {
            const D3D12_RENDER_TARGET_BLEND_DESC rt_blend_desc = {
                false,
                false,
                D3D12_BLEND_ONE,
                D3D12_BLEND_ZERO,
                D3D12_BLEND_OP_ADD,
                D3D12_BLEND_ONE,
                D3D12_BLEND_ZERO,
                D3D12_BLEND_OP_ADD,
                D3D12_LOGIC_OP_NOOP,
                D3D12_COLOR_WRITE_ENABLE_ALL,
            };
            for (int i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
                desc.BlendState.RenderTarget[i] = rt_blend_desc;
            }
        }

        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.FrontCounterClockwise = FALSE;
        desc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        desc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        desc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        desc.RasterizerState.DepthClipEnable = TRUE;
        desc.RasterizerState.MultisampleEnable = FALSE;
        desc.RasterizerState.AntialiasedLineEnable = FALSE;
        desc.RasterizerState.ForcedSampleCount = 0;
        desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        desc.SampleMask = UINT_MAX;
        desc.DepthStencilState.DepthEnable = false;
        desc.DepthStencilState.StencilEnable = false;

        desc.InputLayout.pInputElementDescs = vertex_layout.data();
        desc.InputLayout.NumElements = vertex_layout.size();
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;

        CHECK_ERR(display->device->CreateGraphicsPipelineState(&desc,
                                                               IID_PPV_ARGS(&pipeline_state)));
    }

    ComPtr<ID3D12Fence> fence;
    uint64_t fence_value = 1;
    display->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE fence_evt = CreateEvent(nullptr, false, false, nullptr);

    // Create the command queue and command allocator
    ComPtr<ID3D12CommandQueue> cmd_queue;
    ComPtr<ID3D12CommandAllocator> cmd_allocator;
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    CHECK_ERR(display->device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&cmd_queue)));
    CHECK_ERR(display->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                      IID_PPV_ARGS(&cmd_allocator)));

    // Make the command lists
    ComPtr<ID3D12GraphicsCommandList4> cmd_list;
    CHECK_ERR(display->device->CreateCommandList(0,
                                                 D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 cmd_allocator.Get(),
                                                 nullptr,
                                                 IID_PPV_ARGS(&cmd_list)));
    cmd_list->Close();

    D3D12_RECT screen_bounds = {0};
    screen_bounds.right = win_width;
    screen_bounds.bottom = win_height;

    D3D12_VIEWPORT viewport = {0};
    viewport.Width = static_cast<float>(win_width);
    viewport.Height = static_cast<float>(win_height);
    viewport.MinDepth = D3D12_MIN_DEPTH;
    viewport.MaxDepth = D3D12_MAX_DEPTH;

    const std::string rt_backend = renderer->name();
    const std::string cpu_brand = get_cpu_brand();
    const std::string gpu_brand = display->gpu_brand();
    const std::string image_output = "dxr_ao_bake.png";
    const std::string display_frontend = display->name();

    size_t frame_id = 0;
    float render_time = 0.f;
    float rays_per_second = 0.f;
    glm::vec2 prev_mouse(-2.f);
    bool done = false;
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
                            // camera.rotate(prev_mouse, cur_mouse);
                        } else if (event.motion.state & SDL_BUTTON_RMASK) {
                            // camera.pan(cur_mouse - prev_mouse);
                        }
                    }
                    prev_mouse = cur_mouse;
                } else if (event.type == SDL_MOUSEWHEEL) {
                    // camera.zoom(event.wheel.y * 0.1);
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

        const bool need_readback = save_image;
        /*
        RenderStats stats = renderer->render(
            camera.eye(), camera.dir(), camera.up(), 45.f, true, need_readback);
            */
        // Build the command list to clear the frame color
        CHECK_ERR(cmd_allocator->Reset());

        CHECK_ERR(cmd_list->Reset(cmd_allocator.Get(), pipeline_state.Get()));

        cmd_list->SetGraphicsRootSignature(root_signature.Get());
        cmd_list->RSSetViewports(1, &viewport);
        cmd_list->RSSetScissorRects(1, &screen_bounds);
        D3D12_CPU_DESCRIPTOR_HANDLE render_target =
            rtv_heap->GetCPUDescriptorHandleForHeapStart();
        cmd_list->OMSetRenderTargets(1, &render_target, false, nullptr);

        cmd_list->ClearRenderTargetView(render_target, clear_value.Color, 0, nullptr);
        cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        // Note: The AO baking doesn't support actually having multiple instances of the same
        // mesh
        for (auto &m : renderer->meshes) {
            for (auto &g : m.geometries) {
                std::array<D3D12_VERTEX_BUFFER_VIEW, 3> vbo_views = {
                    D3D12_VERTEX_BUFFER_VIEW{
                        g.vertex_buf->GetGPUVirtualAddress(),
                        static_cast<uint32_t>(g.vertex_buf.size()),
                        sizeof(glm::vec3),
                    },
                    D3D12_VERTEX_BUFFER_VIEW{
                        g.normal_buf->GetGPUVirtualAddress(),
                        static_cast<uint32_t>(g.normal_buf.size()),
                        sizeof(glm::vec3),
                    },
                    D3D12_VERTEX_BUFFER_VIEW{
                        g.uv_buf->GetGPUVirtualAddress(),
                        static_cast<uint32_t>(g.uv_buf.size()),
                        sizeof(glm::vec2),
                    },
                };
                D3D12_INDEX_BUFFER_VIEW indices_view;
                indices_view.BufferLocation = g.index_buf->GetGPUVirtualAddress();
                indices_view.Format = DXGI_FORMAT_R32_UINT;
                indices_view.SizeInBytes = g.index_buf.size();

                cmd_list->IASetVertexBuffers(0, vbo_views.size(), vbo_views.data());
                cmd_list->IASetIndexBuffer(&indices_view);
                cmd_list->DrawIndexedInstanced(
                    g.index_buf.size() / sizeof(uint32_t), 1, 0, 0, 0);
            }
        }
        CHECK_ERR(cmd_list->Close());

        // Execute the command list and present
        std::array<ID3D12CommandList *, 1> cmd_lists = {cmd_list.Get()};
        cmd_queue->ExecuteCommandLists(cmd_lists.size(), cmd_lists.data());

        const uint64_t signal_val = fence_value++;
        CHECK_ERR(cmd_queue->Signal(fence.Get(), signal_val));

        if (fence->GetCompletedValue() < signal_val) {
            CHECK_ERR(fence->SetEventOnCompletion(signal_val, fence_evt));
            WaitForSingleObject(fence_evt, INFINITE);
        }

        ++frame_id;

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

        /*
        if (frame_id == 1) {
            render_time = stats.render_time;
            rays_per_second = stats.rays_per_second;
        } else {
            render_time += stats.render_time;
            rays_per_second += stats.rays_per_second;
        }
        */

        display->new_frame();

        ImGui_ImplSDL2_NewFrame(window);
        ImGui::NewFrame();

        ImGui::Begin("Render Info");
        ImGui::Text("Render Time: %.3f ms/frame (%.1f FPS)",
                    render_time / frame_id,
                    1000.f / (render_time / frame_id));

        /*
        if (stats.rays_per_second > 0) {
            const std::string rays_per_sec = pretty_print_count(rays_per_second / frame_id);
            ImGui::Text("Rays per-second: %sRay/s", rays_per_sec.c_str());
        }
        */

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

        display->display_native(ao_image);
    }
}
