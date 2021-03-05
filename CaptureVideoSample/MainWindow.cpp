#include "pch.h"
#include "MainWindow.h"
#include "App.h"
#include <robmikh.common/ControlsHelper.h>

const std::wstring MainWindow::ClassName = L"CaptureVideoSample.MainWindow";

namespace winrt
{
    using namespace Windows::Foundation::Metadata;
    using namespace Windows::Graphics;
    using namespace Windows::Graphics::Capture;
    using namespace Windows::Storage::Pickers;
    using namespace Windows::System;
}

namespace util
{
    using namespace robmikh::common::desktop::controls;
}

void MainWindow::RegisterWindowClass()
{
    auto instance = winrt::check_pointer(GetModuleHandleW(nullptr));
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = instance;
    wcex.hIcon = LoadIconW(instance, IDI_APPLICATION);
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = ClassName.c_str();
    wcex.hIconSm = LoadIconW(instance, IDI_APPLICATION);
    winrt::check_bool(RegisterClassExW(&wcex));
}

MainWindow::MainWindow(std::wstring const& titleString, int width, int height, std::shared_ptr<App> app)
{
    auto instance = winrt::check_pointer(GetModuleHandleW(nullptr));

    winrt::check_bool(CreateWindowExW(0, ClassName.c_str(), titleString.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr, instance, this));
    WINRT_ASSERT(m_window);

    ShowWindow(m_window, SW_SHOWDEFAULT);
    UpdateWindow(m_window);

    m_app = app;
    m_resolutions =
    {
        { L"1280 x 720", { 1280, 720 } },
        { L"1920 x 1080", { 1920, 1080 } },
        { L"3840 x 2160", { 3840, 2160 } },
        { L"7680 x 4320", { 7680, 4320 } },
        { L"Use source size", { 0, 0 } },
    };
    m_bitRates =
    {
        { L"9 Mbps", 9000000 },
        { L"18 Mbps", 18000000 },
        { L"36 Mbps", 36000000 },
        { L"72 Mbps", 72000000 },
    };
    m_frameRates =
    {
        { L"24 Mbps", 24 },
        { L"30 Mbps", 30 },
        { L"60 Mbps", 60 },
    };

    CreateControls(instance);
}

LRESULT MainWindow::MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam)
{
    switch (message)
    {
    case WM_COMMAND:
    {
        auto command = HIWORD(wparam);
        auto hwnd = (HWND)lparam;
        switch (command)
        {
        case BN_CLICKED:
        {
            if (hwnd == m_mainButton)
            {
                if (m_state == ApplicationState::Idle)
                {
                    StartRecording();
                }
                else if (m_state == ApplicationState::Recording)
                {
                    StopRecording();
                }
            }
            else if (hwnd == m_topMostCheckBox)
            {
                auto value = SendMessageW(m_topMostCheckBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
                auto flag = value ? HWND_TOPMOST : HWND_NOTOPMOST;
                winrt::check_bool(SetWindowPos(m_window, flag, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE));
            }
            else if (hwnd == m_excludeCheckBox)
            {
                auto value = SendMessageW(m_excludeCheckBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
                winrt::check_bool(SetWindowDisplayAffinity(m_window, value ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE));
            }
        }
        break;
        }
    }
    break;
    case WM_CTLCOLORSTATIC:
        return util::StaticControlColorMessageHandler(wparam, lparam);
    default:
        return base_type::MessageHandler(message, wparam, lparam);
    }
    return base_type::MessageHandler(message, wparam, lparam);
}

void MainWindow::CreateControls(HINSTANCE instance)
{
    // Window exclusion
    auto isWin32CaptureExcludePresent = winrt::ApiInformation::IsApiContractPresent(L"Windows.Foundation.UniversalApiContract", 9);

    auto controls = util::StackPanel(m_window, instance, 10, 10, 40, 200, 30);

    m_mainButton = controls.CreateControl(util::ControlType::Button, L"Select Window/Monitor");
    controls.CreateControl(util::ControlType::Label, L"Output resolution:");
    m_resolutionComboBox = controls.CreateControl(util::ControlType::ComboBox, L"");
    controls.CreateControl(util::ControlType::Label, L"Output bit rate:");
    m_bitRateComboBox = controls.CreateControl(util::ControlType::ComboBox, L"");
    controls.CreateControl(util::ControlType::Label, L"Output fps:");
    m_fpsComboBox = controls.CreateControl(util::ControlType::ComboBox, L"");
    m_topMostCheckBox = controls.CreateControl(util::ControlType::CheckBox, L"Make this window topmost");
    m_excludeCheckBox = controls.CreateControl(util::ControlType::CheckBox, L"Exclude this window");
    if (!isWin32CaptureExcludePresent)
    {
        EnableWindow(m_excludeCheckBox, false);
    }

    // Populate resolution combo box
    for (auto& entry : m_resolutions)
    {
        SendMessageW(m_resolutionComboBox, CB_ADDSTRING, 0, (LPARAM)entry.Display.c_str());
    }
    SendMessageW(m_resolutionComboBox, CB_SETCURSEL, m_resolutions.size() - 1, 0);

    // Populate bit rate combo box
    for (auto& entry : m_bitRates)
    {
        SendMessageW(m_bitRateComboBox, CB_ADDSTRING, 0, (LPARAM)entry.Display.c_str());
    }
    SendMessageW(m_bitRateComboBox, CB_SETCURSEL, 1, 0);

    // Populate frame rate combo box
    for (auto& entry : m_frameRates)
    {
        SendMessageW(m_fpsComboBox, CB_ADDSTRING, 0, (LPARAM)entry.Display.c_str());
    }
    SendMessageW(m_fpsComboBox, CB_SETCURSEL, 2, 0);
}

size_t MainWindow::GetIndexFromComboBox(HWND comboBox)
{
    auto index = SendMessageW(comboBox, CB_GETCURSEL, 0, 0);
    WINRT_VERIFY(index != CB_ERR);
    return static_cast<size_t>(index);
}

winrt::fire_and_forget MainWindow::StartRecording()
{
    auto picker = winrt::GraphicsCapturePicker();
    InitializeObjectWithWindowHandle(picker);
    auto item = co_await picker.PickSingleItemAsync();

    if (item != nullptr)
    {
        auto resolution = GetResolution(item);
        auto bitRate = GetBitRate();
        auto frameRate = GetFrameRate();

        OnRecordingStarted();

        auto file = co_await m_app->StartRecordingAsync(item, resolution, bitRate, frameRate);

        auto filePicker = winrt::FileSavePicker();
        InitializeObjectWithWindowHandle(filePicker);
        filePicker.SuggestedStartLocation(winrt::PickerLocationId::VideosLibrary);
        filePicker.SuggestedFileName(L"recording");
        filePicker.DefaultFileExtension(L".mp4");
        filePicker.FileTypeChoices().Clear();
        filePicker.FileTypeChoices().Insert(L"MP4 Video", winrt::single_threaded_vector<winrt::hstring>({ L".mp4" }));
        auto destFile = co_await filePicker.PickSaveFileAsync();
        if (destFile == nullptr)
        {
            co_await file.DeleteAsync();
            co_return;
        }

        co_await file.MoveAndReplaceAsync(destFile);

        OnRecordingFinished();
        co_await winrt::Launcher::LaunchFileAsync(destFile);
    }
    co_return;
}

void MainWindow::OnRecordingStarted()
{
    winrt::check_bool(SetWindowTextW(m_mainButton, L"Stop Recording"));
    EnableWindow(m_resolutionComboBox, false);
    EnableWindow(m_bitRateComboBox, false);
    EnableWindow(m_fpsComboBox, false);
    m_state = ApplicationState::Recording;
}

void MainWindow::OnRecordingFinished()
{
    winrt::check_bool(SetWindowTextW(m_mainButton, L"Select Window\\Monitor"));
    EnableWindow(m_resolutionComboBox, true);
    EnableWindow(m_bitRateComboBox, true);
    EnableWindow(m_fpsComboBox, true);
    m_state = ApplicationState::Idle;
}

winrt::Windows::Graphics::SizeInt32 MainWindow::GetResolution(winrt::GraphicsCaptureItem const& source)
{
    auto index = GetIndexFromComboBox(m_resolutionComboBox);
    if (index == m_resolutions.size() - 1)
    {
        return source.Size();
    }
    else
    {
        auto& entry = m_resolutions[index];
        return entry.Resolution;
    }
}

uint32_t MainWindow::GetBitRate()
{
    auto index = GetIndexFromComboBox(m_bitRateComboBox);
    auto& entry = m_bitRates[index];
    return entry.BitRate;
}

uint32_t MainWindow::GetFrameRate()
{
    auto index = GetIndexFromComboBox(m_fpsComboBox);
    auto& entry = m_frameRates[index];
    return entry.FrameRate;
}

void MainWindow::StopRecording()
{
    m_app->StopRecording();
}
