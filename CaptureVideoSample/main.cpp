#include "pch.h"
#include "MainWindow.h"
#include "App.h"

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Foundation::Numerics;
    using namespace Windows::Foundation::Metadata;
    using namespace Windows::UI;
    using namespace Windows::UI::Composition;
    using namespace Windows::Graphics::Capture;
    using namespace Windows::Media::Core;
}

namespace util
{
    using namespace robmikh::common::desktop;
}

int __stdcall WinMain(HINSTANCE, HINSTANCE, PSTR, int)
{
    // Initialize COM
    winrt::init_apartment();

    // Check to see that we have the minimum required features
    auto isCaptureSupported = winrt::GraphicsCaptureSession::IsSupported();
    auto sampleSupported = winrt::ApiInformation::IsMethodPresent(winrt::name_of<winrt::MediaStreamSample>(), L"CreateFromDirect3D11Surface");
    if (!isCaptureSupported || !sampleSupported)
    {
        MessageBoxW(nullptr,
            L"This release of Windows does not have the minimum required features! Please update to a newer release.",
            L"CaptureVideoSample",
            MB_OK | MB_ICONERROR);
        return 1;
    }
    
    // Register our window classes
    MainWindow::RegisterWindowClass();

    // Create the DispatcherQueue that the compositor needs to run
    auto controller = util::CreateDispatcherQueueControllerForCurrentThread();

    // Create our visual tree
    auto compositor = winrt::Compositor();
    auto root = compositor.CreateContainerVisual();
    root.RelativeSizeAdjustment({ 1.0f, 1.0f });
    root.Size({ -220.0f, 0.0f });
    root.Offset({ 220.0f, 0.0f, 0.0f });

    // Create our app
    auto app = std::make_shared<App>(root);

    // Create our window and connect our visual tree
    auto window = MainWindow(L"CaptureVideoSample", 800, 600, app);
    auto target = window.CreateWindowTarget(compositor);
    target.Root(root);


    // Message pump
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
