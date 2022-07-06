#include "pch.h"
#include "App.h"
#include "VideoRecordingSession.h"

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Graphics;
    using namespace Windows::Graphics::Capture;
    using namespace Windows::Storage;
    using namespace Windows::UI::Composition;
}

namespace util
{
    using namespace robmikh::common::uwp;
}

App::App(winrt::ContainerVisual const& root)
{
    m_compositor = root.Compositor();
    m_root = m_compositor.CreateContainerVisual();
    m_content = m_compositor.CreateSpriteVisual();
    m_brush = m_compositor.CreateSurfaceBrush();

    m_root.RelativeSizeAdjustment({ 1, 1 });
    root.Children().InsertAtTop(m_root);

    m_content.AnchorPoint({ 0.5f, 0.5f });
    m_content.RelativeOffsetAdjustment({ 0.5f, 0.5f, 0 });
    m_content.RelativeSizeAdjustment({ 1, 1 });
    m_content.Size({ -80, -80 });
    m_content.Brush(m_brush);
    m_brush.HorizontalAlignmentRatio(0.5f);
    m_brush.VerticalAlignmentRatio(0.5f);
    m_brush.Stretch(winrt::CompositionStretch::Uniform);
    auto shadow = m_compositor.CreateDropShadow();
    shadow.Mask(m_brush);
    m_content.Shadow(shadow);
    m_root.Children().InsertAtTop(m_content);

    auto d3dDevice = util::CreateD3DDevice();
    auto dxgiDevice = d3dDevice.as<IDXGIDevice>();
    m_device = CreateDirect3DDevice(dxgiDevice.get());
}

App::~App()
{
}

winrt::IAsyncOperation<winrt::StorageFile> App::StartRecordingAsync(
    winrt::GraphicsCaptureItem const& item,
    winrt::SizeInt32 const& resolution,
    uint32_t bitRate,
    uint32_t frameRate)
{
    auto tempFolderPath = std::filesystem::temp_directory_path().wstring();
    OutputDebugStringW(tempFolderPath.c_str());
    auto tempFolder = co_await winrt::StorageFolder::GetFolderFromPathAsync(tempFolderPath);
    auto appFolder = co_await tempFolder.CreateFolderAsync(L"CaptureVideoSample", winrt::CreationCollisionOption::OpenIfExists);
    auto file = co_await appFolder.CreateFileAsync(L"tempRecording.mp4", winrt::CreationCollisionOption::GenerateUniqueName);

    {
        auto stream = co_await file.OpenAsync(winrt::FileAccessMode::ReadWrite);
        m_recordingSession = VideoRecordingSession::Create(
            m_device,
            item,
            resolution,
            bitRate,
            frameRate, 
            stream);

        auto surface = m_recordingSession->CreatePreviewSurface(m_compositor);
        m_brush.Surface(surface);

        co_await m_recordingSession->StartAsync();
    }

    co_return file;
}

void App::StopRecording()
{
    if (m_recordingSession != nullptr)
    {
        m_recordingSession->Close();
        m_brush.Surface(nullptr);
        m_recordingSession = nullptr;
    }
}
