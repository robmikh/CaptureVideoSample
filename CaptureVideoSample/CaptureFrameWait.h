#pragma once

struct CaptureFrame
{
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface FrameTexture;
    winrt::Windows::Graphics::SizeInt32 ContentSize;
    winrt::Windows::Foundation::TimeSpan SystemRelativeTime;
};

class CaptureFrameWait
{
public:
    CaptureFrameWait(
        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& device,
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& item,
        winrt::Windows::Graphics::SizeInt32 const& size);
    ~CaptureFrameWait();

    std::optional<CaptureFrame> TryGetNextFrame();
    void StopCapture();

private:
    void OnFrameArrived(
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
        winrt::Windows::Foundation::IInspectable const& args);

private:
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_device{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{ nullptr };
    wil::shared_event m_nextFrameEvent;
    wil::shared_event m_endEvent;
    wil::shared_event m_closedEvent;

    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame m_currentFrame{ nullptr };
};