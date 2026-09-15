#pragma once

#include <imgui.h>

#include <string>

struct ID3D11Device;

// ImageLoader: decodes a PNG (or anything else WIC supports) from disk into a D3D11 texture +
// shader resource view, and hands back an ImTextureID ready to pass straight to ImGui::Image()/
// ImageButton() (which take an ImTextureRef in this ImGui version -- it implicitly converts from
// ImTextureID, see imgui.h's own ImTextureRef(ImTextureID) constructor). WIC (Windows Imaging
// Component) is a built-in Windows API -- no new third-party dependency, no CMake FetchContent
// changes, consistent with this project's existing "raw Win32/D3D11" style (StandaloneWindow.cpp's
// own device/swapchain setup).
namespace RC::LivingBaseSpawnMenu::ImageLoader
{
    // Call once, after the D3D11 device exists (StandaloneWindow::ThreadMain, right after
    // CreateDeviceD3D succeeds) -- every GetOrLoad() call needs this device to create textures.
    auto Init(ID3D11Device* device) -> void;

    // Loads (or returns the already-cached texture for) the image at `path`. Cheap to call every
    // frame for the same path -- only decodes/uploads once. On failure (file missing, decode
    // error, called before Init()) returns 0 (ImTextureID_Invalid) and sets out_width/out_height
    // to 0 -- callers should render a plain placeholder rectangle/label instead of Image() in that
    // case, never crash or skip the button entirely (a missing thumbnail shouldn't make its whole
    // grid cell unusable).
    auto GetOrLoad(const std::string& path, int& out_width, int& out_height) -> ImTextureID;

    // Releases every cached SRV/texture. Call once at shutdown (StandaloneWindow::ThreadMain,
    // before the D3D11 device itself is torn down) so nothing outlives the device that created it.
    auto ReleaseAll() -> void;
} // namespace RC::LivingBaseSpawnMenu::ImageLoader
