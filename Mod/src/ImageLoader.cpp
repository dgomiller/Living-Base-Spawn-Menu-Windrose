#include <ImageLoader.hpp>

#include <DynamicOutput/DynamicOutput.hpp>

#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <unordered_map>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace RC::LivingBaseSpawnMenu::ImageLoader
{
    namespace
    {
        struct CachedImage
        {
            ComPtr<ID3D11ShaderResourceView> srv;
            int width{};
            int height{};
            bool failed{};
        };

        std::unordered_map<std::string, CachedImage> g_cache;
        ComPtr<ID3D11Device> g_device;
        ComPtr<IWICImagingFactory> g_wic_factory;
        bool g_com_initialized{};

        auto GetWicFactory() -> IWICImagingFactory*
        {
            if (g_wic_factory)
            {
                return g_wic_factory.Get();
            }
            // COM may already be initialized by the game/UE4SS on this thread -- RPC_E_CHANGED_MODE
            // just means "already initialized with a different concurrency model," which is fine
            // for CoCreateInstance, not a real failure. S_FALSE means "already initialized by us,
            // ref-counted" -- also fine.
            HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            g_com_initialized = SUCCEEDED(hr) && hr != RPC_E_CHANGED_MODE;
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic_factory));
            return g_wic_factory.Get();
        }
    } // namespace

    auto Init(ID3D11Device* device) -> void
    {
        g_device = device;
    }

    auto GetOrLoad(const std::string& path, int& out_width, int& out_height) -> ImTextureID
    {
        out_width = out_height = 0;

        auto it = g_cache.find(path);
        if (it != g_cache.end())
        {
            if (it->second.failed)
            {
                return ImTextureID_Invalid;
            }
            out_width = it->second.width;
            out_height = it->second.height;
            return (ImTextureID)(intptr_t)it->second.srv.Get();
        }

        auto cache_failure = [&]() -> ImTextureID
        {
            CachedImage entry{};
            entry.failed = true;
            g_cache[path] = std::move(entry);
            return ImTextureID_Invalid;
        };

        if (!g_device)
        {
            Output::send<LogLevel::Warning>(STR("[LivingBaseSpawnMenu] ImageLoader: GetOrLoad called before Init()\n"));
            return cache_failure();
        }

        IWICImagingFactory* factory = GetWicFactory();
        if (!factory)
        {
            Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] ImageLoader: WIC factory unavailable\n"));
            return cache_failure();
        }

        // Plain ASCII widen -- every swatch path this project controls is ASCII (mod-folder-relative,
        // no user-chosen filenames), so a byte-for-byte widen is safe; a real Unicode path would need
        // MultiByteToWideChar, not needed here.
        std::wstring wpath(path.begin(), path.end());

        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)))
        {
            Output::send<LogLevel::Warning>(STR("[LivingBaseSpawnMenu] ImageLoader: could not open (missing or unsupported): {}\n"), wpath.c_str());
            return cache_failure();
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, &frame)))
        {
            return cache_failure();
        }

        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(&converter)))
        {
            return cache_failure();
        }
        if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
        {
            return cache_failure();
        }

        UINT width = 0, height = 0;
        converter->GetSize(&width, &height);
        if (width == 0 || height == 0)
        {
            return cache_failure();
        }

        std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
        if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data())))
        {
            return cache_failure();
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sub{};
        sub.pSysMem = pixels.data();
        sub.SysMemPitch = width * 4;

        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(g_device->CreateTexture2D(&desc, &sub, &texture)))
        {
            return cache_failure();
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;

        ComPtr<ID3D11ShaderResourceView> srv;
        if (FAILED(g_device->CreateShaderResourceView(texture.Get(), &srv_desc, &srv)))
        {
            return cache_failure();
        }

        CachedImage entry{};
        entry.srv = srv;
        entry.width = static_cast<int>(width);
        entry.height = static_cast<int>(height);
        entry.failed = false;
        ImTextureID id = (ImTextureID)(intptr_t)entry.srv.Get();
        g_cache[path] = std::move(entry);

        out_width = static_cast<int>(width);
        out_height = static_cast<int>(height);
        return id;
    }

    auto ReleaseAll() -> void
    {
        g_cache.clear();
        g_wic_factory.Reset();
        if (g_com_initialized)
        {
            CoUninitialize();
            g_com_initialized = false;
        }
    }
} // namespace RC::LivingBaseSpawnMenu::ImageLoader
