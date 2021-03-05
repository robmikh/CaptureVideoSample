﻿#include "pch.h"
#include "MainWindow.h"
#include "App.h"

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Foundation::Numerics;
    using namespace Windows::UI;
    using namespace Windows::UI::Composition;
}

namespace util
{
    using namespace robmikh::common::desktop;
}

int __stdcall WinMain(HINSTANCE, HINSTANCE, PSTR, int)
{
    // Initialize COM
    winrt::init_apartment();
    
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
