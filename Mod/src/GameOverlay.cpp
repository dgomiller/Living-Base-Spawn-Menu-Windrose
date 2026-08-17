#include <GameOverlay.hpp>

#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <vector>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <polyhook2/Detour/x64Detour.hpp>

#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>

using Microsoft::WRL::ComPtr;

namespace RC::LivingBaseSpawnMenu::GameOverlay
{
    namespace
    {
        using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
        using CreateSwapChainForHwndFn = HRESULT(STDMETHODCALLTYPE*)(
                IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);

        // IDXGISwapChain::Present -- index 8 (see the long comment above HookedCreateSwapChainForHwnd
        // for why this is a real x64Detour and not a vtable swap). IDXGIFactory2::CreateSwapChainForHwnd
        // -- index 15, equally stable/well-known (IUnknown 0-2, IDXGIObject 3-6, IDXGIFactory
        // 7-11 [EnumAdapters, MakeWindowAssociation, GetWindowAssociation, CreateSwapChain,
        // CreateSoftwareAdapter], IDXGIFactory1 12-13 [EnumAdapters1, IsCurrent], IDXGIFactory2
        // starts at 14: IsWindowedStereoEnabled=14, CreateSwapChainForHwnd=15).
        constexpr uint16_t PRESENT_VTABLE_INDEX = 8;
        constexpr uint16_t CREATE_SWAP_CHAIN_FOR_HWND_VTABLE_INDEX = 15;

        PLH::x64Detour* g_present_detour{};
        uint64_t g_present_trampoline{};
        PLH::x64Detour* g_factory_detour{};
        uint64_t g_factory_trampoline{};
        std::atomic_flag g_present_fired{};

        HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swap_chain, UINT sync_interval, UINT flags);

        // Simple free-list SRV descriptor allocator -- same pattern as Dear ImGui's own official
        // example_win32_directx12 sample (imgui-src/examples/example_win32_directx12/main.cpp),
        // which ImGui_ImplDX12_InitInfo's SrvDescriptorAllocFn/FreeFn callbacks expect.
        struct DescriptorHeapAllocator
        {
            ID3D12DescriptorHeap* heap{};
            D3D12_CPU_DESCRIPTOR_HANDLE heap_start_cpu{};
            D3D12_GPU_DESCRIPTOR_HANDLE heap_start_gpu{};
            UINT handle_increment{};
            std::vector<int> free_indices;

            void Create(ID3D12Device* device, ID3D12DescriptorHeap* in_heap)
            {
                heap = in_heap;
                D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
                heap_start_cpu = heap->GetCPUDescriptorHandleForHeapStart();
                heap_start_gpu = heap->GetGPUDescriptorHandleForHeapStart();
                handle_increment = device->GetDescriptorHandleIncrementSize(desc.Type);
                free_indices.reserve(desc.NumDescriptors);
                for (int i = static_cast<int>(desc.NumDescriptors) - 1; i >= 0; --i)
                {
                    free_indices.push_back(i);
                }
            }

            void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu)
            {
                int idx = free_indices.back();
                free_indices.pop_back();
                out_cpu->ptr = heap_start_cpu.ptr + (static_cast<SIZE_T>(idx) * handle_increment);
                out_gpu->ptr = heap_start_gpu.ptr + (static_cast<UINT64>(idx) * handle_increment);
            }

            void Free(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE)
            {
                int idx = static_cast<int>((cpu.ptr - heap_start_cpu.ptr) / handle_increment);
                free_indices.push_back(idx);
            }
        };

        struct FrameContext
        {
            ID3D12CommandAllocator* command_allocator{};
            UINT64 fence_value{};
        };

        // Set the first time a frame actually renders; everything below is only ever touched from
        // the render thread (whichever thread calls the real Present -- always the same thread
        // frame to frame) or from inside HookedCreateSwapChainForHwnd (which only ever runs once,
        // before Present has a chance to fire), so no synchronization needed for this group.
        bool g_imgui_initialized{};
        bool g_imgui_init_failed{};
        ID3D12Device* g_device{};
        ID3D12CommandQueue* g_command_queue{}; // captured via the CreateSwapChainForHwnd hook
        ID3D12DescriptorHeap* g_rtv_heap{};
        ID3D12DescriptorHeap* g_srv_heap{};
        DescriptorHeapAllocator g_srv_heap_alloc{};
        std::vector<ID3D12Resource*> g_back_buffers;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> g_rtv_handles;
        std::vector<FrameContext> g_frame_contexts;
        ID3D12GraphicsCommandList* g_command_list{};
        ID3D12Fence* g_fence{};
        HANDLE g_fence_event{};
        UINT64 g_fence_last_signaled{};

        // No logging (or any other UE4SS/engine API call) inside any of these hooks -- they run
        // on the game's own render/creation threads. on_update() polls HasPresentFired() and logs
        // from there instead, a context already confirmed safe.
        //
        // IMPORTANT, read before touching this file's hooking approach again:
        //
        // 1. An earlier attempt used PLH::VFuncSwapHook (a raw vtable-pointer swap) instead of a
        //    real detour, and crashed the game with a stack overflow inside Steam's own overlay
        //    (gameoverlayrenderer64.dll!OverlayHookD3D3), confirmed via a live cdb session --
        //    Steam's overlay hooks the SAME vtable slot the same way, and swapping the slot again
        //    on top of it created an infinite A-calls-B-calls-A loop between the two hooks.
        //    x64Detour patches the actual function's machine code instead (inline/trampoline
        //    hook), the standard technique specifically because it composes correctly when
        //    something else (Steam, Discord, etc.) is already hooking the same function -- do not
        //    go back to a vtable-swap style hook without a real reason.
        //
        // 2. An earlier attempt captured "whichever command queue calls ExecuteCommandLists
        //    first" as the queue to submit our own draw commands through, filtered only by
        //    DIRECT type. This game has NVIDIA Streamline (DLSS Frame Generation) active
        //    (`FStreamlineD3D12DXGISwapchainProvider` seen in the engine log) AND AMD
        //    FidelityFX/Anti-Lag 2 (confirmed via a live cdb thread dump showing contention
        //    involving `IAntiLag2Module`/`ffxPrintMessage`) -- both wrap the D3D12
        //    present/command-submission pipeline, and per NVIDIA's own Streamline docs, DLSS-G
        //    specifically creates an EXTRA graphics (DIRECT-type) command queue just for
        //    asynchronous presentation. "First DIRECT queue seen" was consequently unreliable and
        //    produced a real hang severe enough to require force-closing the game. Per NVIDIA's
        //    own guidance ("overlays should intercept IDXGIFactory::CreateSwapChainXXX to obtain
        //    the correct swap-chain and command queue"), the fix is to hook swapchain CREATION
        //    instead of guessing: for D3D12, IDXGIFactory2::CreateSwapChainForHwnd's `pDevice`
        //    parameter IS the ID3D12CommandQueue* by documented Microsoft API contract, so
        //    intercepting this ONE call gives us the real queue AND the real swapchain together,
        //    with certainty, at the exact moment the game (or Streamline, on the game's behalf)
        //    actually creates them -- no disposable dummy object's vtable to assume is shared.
        //    Also means we no longer need a disposable dummy device/queue/window at all to find
        //    Present's address -- we read it directly off the REAL swap chain the first time one
        //    is created, which is strictly more reliable than guessing via an unrelated instance.
        HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForHwnd(IDXGIFactory2* factory,
                                                                IUnknown* device,
                                                                HWND hwnd,
                                                                const DXGI_SWAP_CHAIN_DESC1* desc,
                                                                const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen_desc,
                                                                IDXGIOutput* restrict_output,
                                                                IDXGISwapChain1** out_swap_chain)
        {
            HRESULT hr = reinterpret_cast<CreateSwapChainForHwndFn>(g_factory_trampoline)(factory, device, hwnd, desc, fullscreen_desc, restrict_output, out_swap_chain);

            if (SUCCEEDED(hr) && out_swap_chain && *out_swap_chain && !g_present_detour)
            {
                ID3D12CommandQueue* queue{};
                if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&queue))))
                {
                    g_command_queue = queue;
                }

                void** swap_chain_vtable = *reinterpret_cast<void***>(*out_swap_chain);
                void* present_addr = swap_chain_vtable[PRESENT_VTABLE_INDEX];

                g_present_detour = new PLH::x64Detour((uint64_t)present_addr, (uint64_t)&HookedPresent, &g_present_trampoline);
                g_present_detour->hook();
            }

            return hr;
        }

        // Lazy one-time setup, called from HookedPresent the first time g_command_queue is known.
        // Every failure path bails out and sets g_imgui_init_failed so we quietly stop trying
        // rather than repeat a failing setup (or worse, crash) every single frame.
        bool InitImGui(IDXGISwapChain* swap_chain)
        {
            if (FAILED(swap_chain->GetDevice(IID_PPV_ARGS(&g_device))))
            {
                return false;
            }

            DXGI_SWAP_CHAIN_DESC desc{};
            if (FAILED(swap_chain->GetDesc(&desc)))
            {
                return false;
            }
            HWND hwnd = desc.OutputWindow;
            if (!hwnd)
            {
                return false;
            }
            UINT buffer_count = desc.BufferCount > 0 ? desc.BufferCount : 2;
            DXGI_FORMAT rtv_format = desc.BufferDesc.Format != DXGI_FORMAT_UNKNOWN ? desc.BufferDesc.Format : DXGI_FORMAT_R8G8B8A8_UNORM;

            D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
            rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtv_heap_desc.NumDescriptors = buffer_count;
            if (FAILED(g_device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&g_rtv_heap))))
            {
                return false;
            }

            UINT rtv_increment = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = g_rtv_heap->GetCPUDescriptorHandleForHeapStart();
            g_back_buffers.resize(buffer_count);
            g_rtv_handles.resize(buffer_count);
            for (UINT i = 0; i < buffer_count; ++i)
            {
                ID3D12Resource* buffer{};
                if (FAILED(swap_chain->GetBuffer(i, IID_PPV_ARGS(&buffer))))
                {
                    return false;
                }
                g_device->CreateRenderTargetView(buffer, nullptr, rtv_handle);
                g_back_buffers[i] = buffer;
                g_rtv_handles[i] = rtv_handle;
                rtv_handle.ptr += rtv_increment;
            }

            D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc{};
            srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            srv_heap_desc.NumDescriptors = 64;
            srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (FAILED(g_device->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&g_srv_heap))))
            {
                return false;
            }
            g_srv_heap_alloc.Create(g_device, g_srv_heap);

            g_frame_contexts.resize(buffer_count);
            for (auto& frame : g_frame_contexts)
            {
                if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.command_allocator))))
                {
                    return false;
                }
            }

            if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frame_contexts[0].command_allocator, nullptr, IID_PPV_ARGS(&g_command_list))))
            {
                return false;
            }
            g_command_list->Close();

            if (FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence))))
            {
                return false;
            }
            g_fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!g_fence_event)
            {
                return false;
            }

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGui::StyleColorsDark();

            if (!ImGui_ImplWin32_Init(hwnd))
            {
                return false;
            }

            ImGui_ImplDX12_InitInfo init_info{};
            init_info.Device = g_device;
            init_info.CommandQueue = g_command_queue;
            init_info.NumFramesInFlight = static_cast<int>(buffer_count);
            init_info.RTVFormat = rtv_format;
            init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
            init_info.SrvDescriptorHeap = g_srv_heap;
            init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) { g_srv_heap_alloc.Alloc(cpu, gpu); };
            init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) { g_srv_heap_alloc.Free(cpu, gpu); };
            if (!ImGui_ImplDX12_Init(&init_info))
            {
                return false;
            }

            return true;
        }

        // Renders one frame of our overlay into the swapchain's current back buffer, using the
        // REAL command queue captured in HookedCreateSwapChainForHwnd (not a separate queue of
        // our own) -- submitting GPU work against a resource via a different queue than the one
        // that last touched it is unsafe in D3D12's explicit synchronization model, so this only
        // works correctly because we share the game's actual queue.
        //
        // Phase 4b scope: a single static test window, no input handling yet (that's a separate,
        // later, separately-tested step -- see the WndProc-subclassing/mouse-capture design notes
        // from planning). Every failure path here bails out silently (skips this frame) rather
        // than risking a crash -- this runs on the render thread, every frame, indefinitely.
        void RenderOverlay(IDXGISwapChain* swap_chain)
        {
            if (g_imgui_init_failed)
            {
                return;
            }
            if (!g_command_queue)
            {
                // HookedCreateSwapChainForHwnd hasn't captured a queue yet (or the QueryInterface
                // to ID3D12CommandQueue failed) -- nothing to submit our draw commands through.
                return;
            }
            if (!g_imgui_initialized)
            {
                if (!InitImGui(swap_chain))
                {
                    g_imgui_init_failed = true;
                    return;
                }
                g_imgui_initialized = true;
            }

            ComPtr<IDXGISwapChain3> swap_chain3;
            if (FAILED(swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain3))))
            {
                return;
            }
            UINT back_buffer_index = swap_chain3->GetCurrentBackBufferIndex();
            if (back_buffer_index >= g_frame_contexts.size())
            {
                return;
            }

            // Bounded wait, NOT INFINITE -- an earlier version waited forever here and, when the
            // captured queue turned out not to be the one actually driving this fence, genuinely
            // hung the game hard enough that it had to be force-closed, not just crashed. If this
            // ever times out, skip this frame's overlay entirely (no ImGui::NewFrame() this frame
            // either) and try again next Present -- a dropped overlay frame is fine, hanging the
            // game is not.
            FrameContext& frame = g_frame_contexts[back_buffer_index];
            if (frame.fence_value != 0 && g_fence->GetCompletedValue() < frame.fence_value)
            {
                g_fence->SetEventOnCompletion(frame.fence_value, g_fence_event);
                if (WaitForSingleObject(g_fence_event, 1000) != WAIT_OBJECT_0)
                {
                    return;
                }
            }

            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            ImGui::Begin("LivingBaseSpawnMenu");
            ImGui::Text("Phase 4 overlay test -- rendering into the real game frame.");
            ImGui::End();

            ImGui::Render();

            frame.command_allocator->Reset();
            g_command_list->Reset(frame.command_allocator, nullptr);

            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = g_back_buffers[back_buffer_index];
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            g_command_list->ResourceBarrier(1, &barrier);

            g_command_list->OMSetRenderTargets(1, &g_rtv_handles[back_buffer_index], FALSE, nullptr);
            g_command_list->SetDescriptorHeaps(1, &g_srv_heap);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_command_list);

            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            g_command_list->ResourceBarrier(1, &barrier);
            g_command_list->Close();

            ID3D12CommandList* lists[] = {g_command_list};
            g_command_queue->ExecuteCommandLists(1, lists);

            UINT64 fence_value = ++g_fence_last_signaled;
            g_command_queue->Signal(g_fence, fence_value);
            frame.fence_value = fence_value;
        }

        HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swap_chain, UINT sync_interval, UINT flags)
        {
            g_present_fired.test_and_set();
            RenderOverlay(swap_chain);
            return reinterpret_cast<PresentFn>(g_present_trampoline)(swap_chain, sync_interval, flags);
        }

        // Reads CreateSwapChainForHwnd's address off a plain DXGI factory instance -- unlike the
        // old approach for Present (a disposable SWAPCHAIN's vtable, which may not be shared with
        // the real one once Streamline wraps it), this only needs an IDXGIFactory2, and factory
        // creation itself is process-global: whatever CreateDXGIFactory1 resolves to right now
        // (including any system-wide hook Streamline has already installed on it) is exactly what
        // the game also gets when it calls the same function in this same process, so there's no
        // version-mismatch risk here the way there was for swapchain instances.
        bool find_create_swapchain_address(void** out_addr)
        {
            ComPtr<IDXGIFactory2> factory;
            if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] find_create_swapchain_address: CreateDXGIFactory1 failed\n"));
                return false;
            }

            void** vtable = *reinterpret_cast<void***>(factory.Get());
            *out_addr = vtable[CREATE_SWAP_CHAIN_FOR_HWND_VTABLE_INDEX];
            return true;
        }
    } // namespace

    auto Install() -> bool
    {
        void* create_swap_chain_addr{};
        if (!find_create_swapchain_address(&create_swap_chain_addr))
        {
            Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] Failed to locate CreateSwapChainForHwnd -- overlay disabled\n"));
            return false;
        }

        g_factory_detour = new PLH::x64Detour((uint64_t)create_swap_chain_addr, (uint64_t)&HookedCreateSwapChainForHwnd, &g_factory_trampoline);
        if (!g_factory_detour->hook())
        {
            Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] CreateSwapChainForHwnd x64Detour::hook() failed\n"));
            return false;
        }

        // Present itself gets hooked lazily inside HookedCreateSwapChainForHwnd, the first time a
        // real swap chain is actually created -- see that function's own comment for why.
        Output::send<LogLevel::Normal>(STR("[LivingBaseSpawnMenu] CreateSwapChainForHwnd detour installed\n"));
        return true;
    }

    auto HasPresentFired() -> bool
    {
        return g_present_fired.test();
    }
} // namespace RC::LivingBaseSpawnMenu::GameOverlay
