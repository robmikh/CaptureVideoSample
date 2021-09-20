#pragma once
#include "CaptureFrameWait.h"
#include "VideoEncoder.h"
#include "VideoEncoderDevice.h"

class VideoRecordingSession
{
public:
    VideoRecordingSession(
        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& device,
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& item,
        std::shared_ptr<VideoEncoderDevice> const& encoderDevice,
        winrt::Windows::Graphics::SizeInt32 const& resolution,
        uint32_t bitRate,
        uint32_t frameRate,
        winrt::Windows::Storage::Streams::IRandomAccessStream const& stream);
    ~VideoRecordingSession();

    winrt::Windows::Foundation::IAsyncAction StartAsync();
    void Close();
    winrt::Windows::UI::Composition::ICompositionSurface CreatePreviewSurface(winrt::Windows::UI::Composition::Compositor const& compositor);

private:
    void CloseInternal();

    void OnMediaStreamSourceStarting(
        winrt::Windows::Media::Core::MediaStreamSource const& sender,
        winrt::Windows::Media::Core::MediaStreamSourceStartingEventArgs const& args);
    void OnMediaStreamSourceSampleRequested(
        winrt::Windows::Media::Core::MediaStreamSource const& sender,
        winrt::Windows::Media::Core::MediaStreamSourceSampleRequestedEventArgs const& args);

    std::optional<std::unique_ptr<InputSample>> OnSampleRequested();
    void OnSampleRendered(std::unique_ptr<OutputSample> sample);

private:
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_device{ nullptr };
    winrt::com_ptr<ID3D11Device> m_d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> m_d3dContext;

    winrt::com_ptr<ID3D11VideoDevice> m_videoDevice;
    winrt::com_ptr<ID3D11VideoContext> m_videoContext;
    winrt::com_ptr<ID3D11VideoProcessor> m_videoProcessor;
    winrt::com_ptr<ID3D11Texture2D> m_videoOutputTexture;
    winrt::com_ptr<ID3D11VideoProcessorOutputView> m_videoOutput;
    winrt::com_ptr<ID3D11Texture2D> m_videoInputTexture;
    winrt::com_ptr<ID3D11VideoProcessorInputView> m_videoInput;

    winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem::Closed_revoker m_itemClosed;
    std::shared_ptr<CaptureFrameWait> m_frameWait;

    std::shared_ptr<VideoEncoder> m_videoEncoder;

    winrt::Windows::Storage::Streams::IRandomAccessStream m_stream{ nullptr };
    winrt::com_ptr<IMFSinkWriter> m_sinkWriter;
    DWORD m_sinkWriterStreamIndex = 0;
    int64_t m_frameDuration = 0;
    bool m_seenFirstTimeStamp = false;
    winrt::Windows::Foundation::TimeSpan m_firstTimeStamp;

    winrt::com_ptr<IDXGISwapChain1> m_previewSwapChain;
    winrt::com_ptr<ID3D11RenderTargetView> m_renderTargetView;

    std::atomic<bool> m_isRecording = false;
    std::atomic<bool> m_closed = false;
};