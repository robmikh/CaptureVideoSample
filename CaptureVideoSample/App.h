#pragma once

class VideoRecordingSession;

class App
{
public:
    App(winrt::Windows::UI::Composition::ContainerVisual const& root);
    ~App();

    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Storage::StorageFile> StartRecordingAsync(
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& item,
        winrt::Windows::Graphics::SizeInt32 const& resolution,
        uint32_t bitRate,
        uint32_t frameRate);
    void StopRecording();

private:
    winrt::Windows::UI::Composition::Compositor m_compositor{ nullptr };
    winrt::Windows::UI::Composition::ContainerVisual m_root{ nullptr };
    winrt::Windows::UI::Composition::SpriteVisual m_content{ nullptr };
    winrt::Windows::UI::Composition::CompositionSurfaceBrush m_brush{ nullptr };

    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_device{ nullptr };
    std::unique_ptr<VideoRecordingSession> m_recordingSession;
};