#pragma once

class CaptureFrameGenerator
{
public:
    CaptureFrameGenerator(
        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& device,
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& item,
        winrt::Windows::Graphics::SizeInt32 const& size);
    ~CaptureFrameGenerator();

    std::optional<winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame> TryGetNextFrame();
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
    wil::srwlock m_lock;
    std::deque<winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame> m_frames;
    winrt::Windows::Foundation::TimeSpan m_lastSeenTimestmap = {};
};
