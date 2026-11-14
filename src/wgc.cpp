#include "internal.hpp"

#ifndef _WIN32
#error "src/wgc.cpp requires Win32"
#endif

#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <thread>

namespace stronghold {
namespace {

using Microsoft::WRL::ComPtr;
namespace capture = winrt::Windows::Graphics::Capture;
namespace dx = winrt::Windows::Graphics::DirectX;
namespace d3d = winrt::Windows::Graphics::DirectX::Direct3D11;

// Microsoft Windows.UI.Composition Win32 sample demonstrates CreateForWindow
// and the Windows.Graphics.Capture + D3D11 frame-pool flow (MIT licensed):
// https://github.com/microsoft/Windows.UI.Composition-Win32-Samples/tree/master/cpp/ScreenCaptureforHWND

class Apartment final {
public:
    Apartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
    ~Apartment() { winrt::uninit_apartment(); }
    Apartment(const Apartment&) = delete;
    Apartment& operator=(const Apartment&) = delete;
};

std::string hresult_hex(HRESULT hr) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
        << static_cast<std::uint32_t>(hr);
    return out.str();
}

[[noreturn]] void throw_hresult(const char* operation, HRESULT hr) {
    throw Error("wgc_hresult", std::string(operation) + " failed with HRESULT " + hresult_hex(hr) + ".");
}

[[noreturn]] void throw_win32(const char* operation, DWORD code = GetLastError()) {
    std::ostringstream out;
    out << operation << " failed with Win32 error " << code << '.';
    throw Error("wgc_win32_error", out.str());
}

struct ClientCrop {
    LONG left = 0;
    LONG top = 0;
    LONG width = 0;
    LONG height = 0;
    LONG frame_width = 0;
    LONG frame_height = 0;
};

ClientCrop client_crop(HWND hwnd, const Window& expected) {
    RECT client{};
    if (!GetClientRect(hwnd, &client)) throw_win32("GetClientRect");
    POINT client_origin{ client.left, client.top };
    if (!ClientToScreen(hwnd, &client_origin)) throw_win32("ClientToScreen");

    const LONG client_width = client.right - client.left;
    const LONG client_height = client.bottom - client.top;
    if (client_width != expected.width || client_height != expected.height ||
        client_origin.x != expected.left || client_origin.y != expected.top) {
        throw Error("window_changed", "The game window client geometry changed during WGC capture.");
    }

    RECT frame_bounds{};
    const HRESULT bounds_hr = DwmGetWindowAttribute(
        hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame_bounds, sizeof(frame_bounds));
    if (FAILED(bounds_hr)) throw_hresult("DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)", bounds_hr);

    ClientCrop crop;
    crop.left = client_origin.x - frame_bounds.left;
    crop.top = client_origin.y - frame_bounds.top;
    crop.width = client_width;
    crop.height = client_height;
    crop.frame_width = frame_bounds.right - frame_bounds.left;
    crop.frame_height = frame_bounds.bottom - frame_bounds.top;
    if (crop.left < 0 || crop.top < 0 || crop.width <= 0 || crop.height <= 0 ||
        crop.frame_width <= 0 || crop.frame_height <= 0 ||
        crop.left > crop.frame_width - crop.width ||
        crop.top > crop.frame_height - crop.height) {
        throw Error("wgc_client_crop_invalid", "The client area does not fit within DWM extended frame bounds.");
    }
    return crop;
}

struct MappedResource final {
    ID3D11DeviceContext* context = nullptr;
    ID3D11Resource* resource = nullptr;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    ~MappedResource() {
        if (context && resource && mapped.pData) context->Unmap(resource, 0);
    }
};

struct CaptureResources final {
    capture::GraphicsCaptureSession session{nullptr};
    capture::Direct3D11CaptureFramePool pool{nullptr};
    ~CaptureResources() {
        try { if (session) session.Close(); } catch (...) {}
        try { if (pool) pool.Close(); } catch (...) {}
    }
};

winrt::com_ptr<IGraphicsCaptureItemInterop> capture_item_interop() {
    auto factory = winrt::get_activation_factory<capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    return factory;
}

Frame make_rgb_frame(const capture::Direct3D11CaptureFrame& captured,
                     const ClientCrop& crop,
                     ID3D11Device* device,
                     ID3D11DeviceContext* context,
                     const winrt::Windows::Graphics::SizeInt32& configured_size) {
    const auto content = captured.ContentSize();
    if (content.Width != configured_size.Width || content.Height != configured_size.Height ||
        content.Width != crop.frame_width || content.Height != crop.frame_height) {
        throw Error("wgc_content_size_changed", "WGC frame content size differs from the verified window frame geometry.");
    }

    ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> surface_access;
    const auto surface = captured.Surface();
    const HRESULT access_hr = reinterpret_cast<IUnknown*>(winrt::get_abi(surface))
        ->QueryInterface(IID_PPV_ARGS(&surface_access));
    if (FAILED(access_hr)) throw_hresult("IDirect3DSurface::QueryInterface(IDirect3DDxgiInterfaceAccess)", access_hr);
    ComPtr<ID3D11Texture2D> source;
    const HRESULT get_texture_hr = surface_access->GetInterface(IID_PPV_ARGS(&source));
    if (FAILED(get_texture_hr)) throw_hresult("IDirect3DDxgiInterfaceAccess::GetInterface", get_texture_hr);

    D3D11_TEXTURE2D_DESC source_desc{};
    source->GetDesc(&source_desc);
    if (source_desc.Width != static_cast<UINT>(configured_size.Width) ||
        source_desc.Height != static_cast<UINT>(configured_size.Height) ||
        source_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
        crop.left > static_cast<LONG>(source_desc.Width) - crop.width ||
        crop.top > static_cast<LONG>(source_desc.Height) - crop.height) {
        throw Error("wgc_texture_size_invalid", "WGC texture size or format is incompatible with the client crop.");
    }

    D3D11_TEXTURE2D_DESC staging_desc = source_desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.MiscFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    const HRESULT create_hr = device->CreateTexture2D(&staging_desc, nullptr, &staging);
    if (FAILED(create_hr)) throw_hresult("ID3D11Device::CreateTexture2D(staging)", create_hr);

    context->CopyResource(staging.Get(), source.Get());
    MappedResource mapped{ context, staging.Get(), {} };
    const HRESULT map_hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped.mapped);
    if (FAILED(map_hr)) throw_hresult("ID3D11DeviceContext::Map", map_hr);

    const auto* source_bytes = static_cast<const std::uint8_t*>(mapped.mapped.pData);
    if (!source_bytes || mapped.mapped.RowPitch < source_desc.Width * 4u) {
        throw Error("wgc_row_pitch_invalid", "The mapped WGC texture has an invalid row pitch.");
    }
    auto result = frame_from_bgra_rows(crop.width, crop.height,
        {source_bytes, static_cast<std::size_t>(mapped.mapped.RowPitch) * source_desc.Height},
        mapped.mapped.RowPitch, crop.left, crop.top);
    result.source_time_100ns = captured.SystemRelativeTime().count();
    return result;
}

} // namespace

void check_wgc_support() {
    try {
        Apartment apartment;
        if (!capture::GraphicsCaptureSession::IsSupported()) {
            throw Error("wgc_unsupported", "Windows Graphics Capture is not supported on this device.");
        }
    } catch (const Error&) {
        throw;
    } catch (const winrt::hresult_error& error) {
        throw_hresult("GraphicsCaptureSession::IsSupported", error.code());
    }
}

Frame capture_wgc(const Window& expected, std::chrono::milliseconds timeout) {
    constexpr auto hard_deadline = std::chrono::seconds(8);
    const auto effective_timeout = std::min(timeout, std::chrono::duration_cast<std::chrono::milliseconds>(hard_deadline));
    if (effective_timeout <= std::chrono::milliseconds::zero()) {
        throw Error("wgc_timeout", "The WGC capture deadline has already expired.");
    }

    try {
        Apartment apartment;
        verify_window(expected);
        if (!capture::GraphicsCaptureSession::IsSupported()) {
            throw Error("wgc_unsupported", "Windows Graphics Capture is not supported on this device.");
        }

        const HWND hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(expected.hwnd));
        const ClientCrop crop = client_crop(hwnd, expected);
        auto interop = capture_item_interop();
        capture::GraphicsCaptureItem item{ nullptr };
        const HRESULT item_hr = interop->CreateForWindow(
            hwnd, winrt::guid_of<capture::GraphicsCaptureItem>(), winrt::put_abi(item));
        if (FAILED(item_hr)) throw_hresult("IGraphicsCaptureItemInterop::CreateForWindow", item_hr);

        const auto item_size = item.Size();
        if (item_size.Width <= 0 || item_size.Height <= 0 ||
            item_size.Width != crop.frame_width || item_size.Height != crop.frame_height) {
            throw Error("wgc_item_size_invalid", "GraphicsCaptureItem size does not match the verified DWM frame bounds.");
        }
        (void)pixel_bytes(item_size.Width, item_size.Height);

        ComPtr<ID3D11Device> d3d_device;
        ComPtr<ID3D11DeviceContext> d3d_context;
        D3D_FEATURE_LEVEL feature_level{};
        const HRESULT device_hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION, &d3d_device, &feature_level, &d3d_context);
        if (FAILED(device_hr)) throw_hresult("D3D11CreateDevice", device_hr);

        ComPtr<IDXGIDevice> dxgi_device;
        const HRESULT dxgi_hr = d3d_device.As(&dxgi_device);
        if (FAILED(dxgi_hr)) throw_hresult("ID3D11Device::QueryInterface(IDXGIDevice)", dxgi_hr);
        winrt::com_ptr<IInspectable> inspectable_device;
        const HRESULT winrt_device_hr = CreateDirect3D11DeviceFromDXGIDevice(
            dxgi_device.Get(), inspectable_device.put());
        if (FAILED(winrt_device_hr)) throw_hresult("CreateDirect3D11DeviceFromDXGIDevice", winrt_device_hr);
        auto winrt_device = inspectable_device.as<d3d::IDirect3DDevice>();

        auto frame_pool = capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            winrt_device, dx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, item_size);
        auto session = frame_pool.CreateCaptureSession(item);
        CaptureResources close_on_exit{session, frame_pool};
        try {
            session.IsCursorCaptureEnabled(false);
        } catch (const winrt::hresult_error& error) {
            throw Error("wgc_cursor_disable_unsupported",
                        "The platform could not disable cursor capture (HRESULT " + hresult_hex(error.code()) + ").");
        }
        // Keep the operating-system capture border policy unchanged. In particular, do not request
        // Borderless access or silently change it; the system may visibly mark active capture.
        session.StartCapture();

        const auto deadline = std::chrono::steady_clock::now() + effective_timeout;
        bool saw_frame = false;
        bool saw_stale_frame = false;
        for (;;) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                if (saw_stale_frame) {
                    throw Error("wgc_stale_frame", "WGC did not provide a source frame newer than one second before the deadline.");
                }
                throw Error(saw_frame ? "wgc_stale_frame" : "wgc_timeout",
                            saw_frame ? "WGC frames lacked a usable source timestamp." :
                                        "No WGC frame arrived before the capture deadline.");
            }
            // The supervising parent checks window conditions at 1 Hz. Keep the
            // worker's mandatory before/after checks, without duplicating that poll.

            auto captured = frame_pool.TryGetNextFrame();
            if (!captured) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            saw_frame = true;
            const std::int64_t source_time = captured.SystemRelativeTime().count();
            if (source_time <= 0) {
                saw_stale_frame = true;
                continue;
            }
            Frame timing;
            timing.source_time_100ns = source_time;
            if (frame_age_ms(timing, monotonic_100ns()) > 1000.0) {
                saw_stale_frame = true;
                continue;
            }
            Frame result = make_rgb_frame(captured, crop, d3d_device.Get(), d3d_context.Get(), item_size);
            result.source_time_100ns = source_time;
            verify_window(expected);
            const auto age_ms = frame_age_ms(result, monotonic_100ns());
            if (age_ms < 0.0 || age_ms > 1000.0) {
                saw_stale_frame = true;
                continue;
            }
            return result;
        }
    } catch (const Error&) {
        throw;
    } catch (const winrt::hresult_error& error) {
        throw_hresult("Windows Graphics Capture", error.code());
    }
}

} // namespace stronghold
