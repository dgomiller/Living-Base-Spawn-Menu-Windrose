cmake_minimum_required(VERSION 3.22)
set(TARGET LivingBaseSpawnMenu)
project(${TARGET})

# RE-UE4SS's own ImGui target (deps/third/imgui/CMakeLists.txt) already compiles the Win32 backend
# by default on Windows, but not the DX12 backend (it only ever needed DX11/OpenGL3/GLFW for its
# own separate debug window). Reuse the SAME already-fetched ImGui source dir for
# imgui_impl_dx12.cpp rather than re-fetching it, so we stay on the exact same ImGui version.
include(FetchContent)
FetchContent_GetProperties(ImGui SOURCE_DIR ImGui_SOURCE_DIR)

add_library(${TARGET} SHARED
        src/dllmain.cpp
        src/SpawnMenuMod.cpp
        src/GameOverlay.cpp
        src/StandaloneWindow.cpp
        src/SpawnMenu.cpp
        src/MoveMenu.cpp
        src/MenuStatus.cpp
        src/CoordsMenu.cpp
        src/HelpMenu.cpp
        "${ImGui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp"
)

target_include_directories(${TARGET} PRIVATE "include")
target_link_libraries(${TARGET} PRIVATE ImGui)
target_link_libraries(${TARGET} PRIVATE d3d12 dxgi)
target_link_libraries(${TARGET} PUBLIC UE4SS)
