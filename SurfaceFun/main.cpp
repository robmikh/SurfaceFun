#include "pch.h"
#include "MainWindow.h"

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Foundation::Numerics;
    using namespace Windows::Graphics::DirectX;
    using namespace Windows::UI;
    using namespace Windows::UI::Composition;
}

namespace abi
{
    using namespace ::ABI::Windows::UI::Composition;
}

namespace util
{
    using namespace robmikh::common::desktop;
    using namespace robmikh::common::uwp;
}

winrt::com_ptr<ID2D1Bitmap1> CreateBitmapFromTexture(
    winrt::com_ptr<ID3D11Texture2D> const& texture,
    winrt::com_ptr<ID2D1DeviceContext> const& surfaceContext);

int __stdcall WinMain(HINSTANCE, HINSTANCE, PSTR, int)
{
    // Initialize COM
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    // Create the DispatcherQueue that the compositor needs to run
    auto controller = util::CreateDispatcherQueueControllerForCurrentThread();

    // Create our window and visual tree
    auto window = MainWindow(L"SurfaceFun", 800, 600);
    auto compositor = winrt::Compositor();
    auto target = window.CreateWindowTarget(compositor);
    auto root = compositor.CreateSpriteVisual();
    root.RelativeSizeAdjustment({ 1.0f, 1.0f });
    root.Brush(compositor.CreateColorBrush(winrt::Colors::White()));
    target.Root(root);

    // Turn this on if you want to get debug messages. Keep in mind that
    // you'll need to install the "Graphics Tools" package in the 
    // optional features settings page in the Settings app.
    bool debug = true;
    uint32_t d3dFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    auto d2dDebugLevel = D2D1_DEBUG_LEVEL_NONE;
    if (debug)
    {
        d3dFlags |= D3D11_CREATE_DEVICE_DEBUG;
        d2dDebugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
    }

    auto d3dDevice = util::CreateD3DDevice(d3dFlags);
    auto d2dFactory = util::CreateD2DFactory(d2dDebugLevel);
    auto d2dDevice = util::CreateD2DDevice(d2dFactory, d3dDevice);
    winrt::com_ptr<ID2D1DeviceContext> d2dContext;
    winrt::check_hresult(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContext.put()));
    auto compGraphics = util::CreateCompositionGraphicsDevice(compositor, d2dDevice.get());

    auto surface = compGraphics.CreateDrawingSurface2({ 50, 50 }, winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized, winrt::DirectXAlphaMode::Premultiplied);
    auto visual = compositor.CreateSpriteVisual();
    visual.Size({ 50, 50 });
    visual.Brush(compositor.CreateSurfaceBrush(surface));
    root.Children().InsertAtTop(visual);

    // First, let's clear the surface
    {
        auto surfaceInterop = surface.as<abi::ICompositionDrawingSurfaceInterop>();
        winrt::com_ptr<ID2D1DeviceContext> surfaceContext;
        POINT offset = {};
        winrt::check_hresult(surfaceInterop->BeginDraw(nullptr, winrt::guid_of<ID2D1DeviceContext>(), surfaceContext.put_void(), &offset));
        auto endDraw = wil::scope_exit([surfaceInterop]()
            {
                winrt::check_hresult(surfaceInterop->EndDraw());
            });

        auto color = D2D1_COLOR_F{ 1.0f, 0.0f, 0.0f, 1.0f };
        surfaceContext->Clear(&color);
    }

    // Next, let's create a texture to hold our copy
    winrt::com_ptr<ID3D11Texture2D> copyTexture;
    {
        auto size = surface.SizeInt32();

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = static_cast<uint32_t>(size.Width);
        desc.Height = static_cast<uint32_t>(size.Height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, copyTexture.put()));
    }
    auto copyBitmap = CreateBitmapFromTexture(copyTexture, d2dContext);

    // We'll use this for drawing later
    winrt::com_ptr<ID2D1SolidColorBrush> debugBrush;
    {
        auto debugColor = D2D1_COLOR_F{ 0.0f, 1.0f, 0.0f, 0.5f };
        winrt::check_hresult(d2dContext->CreateSolidColorBrush(debugColor, debugBrush.put()));
    }

    // Now, let's copy the surface contents
    {
        auto surfaceInterop = surface.as<abi::ICompositionDrawingSurfaceInterop2>();
        winrt::check_hresult(surfaceInterop->CopySurface(copyTexture.get(), 0, 0, nullptr));
    }

    // Draw the copy back into the surface and then draw over it
    {
        auto surfaceInterop = surface.as<abi::ICompositionDrawingSurfaceInterop>();
        winrt::com_ptr<ID2D1DeviceContext> surfaceContext;
        POINT offset = {};
        winrt::check_hresult(surfaceInterop->BeginDraw(nullptr, winrt::guid_of<ID2D1DeviceContext>(), surfaceContext.put_void(), &offset));
        auto endDraw = wil::scope_exit([surfaceInterop]()
            {
                winrt::check_hresult(surfaceInterop->EndDraw());
            });

        // Technically this clear is unneccessary, it's just here for debugging
        auto color = D2D1_COLOR_F{ 0.0f, 0.0f, 1.0f, 1.0f };
        surfaceContext->Clear(&color);

        surfaceContext->SetTransform(D2D1::Matrix3x2F::Translation({ static_cast<float>(offset.x), static_cast<float>(offset.y) }));

        surfaceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
        surfaceContext->DrawBitmap(copyBitmap.get());

        surfaceContext->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
        auto rect = D2D1_RECT_F{ 25.0f, 25.0f, 50.0f, 50.0f };
        surfaceContext->FillRectangle(&rect, debugBrush.get());
    }

    // Message pump
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return util::ShutdownDispatcherQueueControllerAndWait(controller, static_cast<int>(msg.wParam));
}

winrt::com_ptr<ID2D1Bitmap1> CreateBitmapFromTexture(
    winrt::com_ptr<ID3D11Texture2D> const& texture,
    winrt::com_ptr<ID2D1DeviceContext> const& surfaceContext)
{
    auto dxgiSurface = texture.as<IDXGISurface>();
    winrt::com_ptr<ID2D1Bitmap1> bitmap;
    winrt::check_hresult(surfaceContext->CreateBitmapFromDxgiSurface(dxgiSurface.get(), nullptr, bitmap.put()));
    return bitmap;
}
