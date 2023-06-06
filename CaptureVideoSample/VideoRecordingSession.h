#pragma once
#include "CaptureFrameGenerator.h"

class VideoRecordingSession : public std::enable_shared_from_this<VideoRecordingSession>
{
public:
    [[nodiscard]] static std::shared_ptr<VideoRecordingSession> Create(
        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& device,
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& item,
        winrt::Windows::Graphics::SizeInt32 const& resolution,
        uint32_t bitRate,
        uint32_t frameRate,
        winrt::Windows::Storage::Streams::IRandomAccessStream const& stream);
    ~VideoRecordingSession();

    winrt::Windows::Foundation::IAsyncAction StartAsync();
    void Close();
    winrt::Windows::UI::Composition::ICompositionSurface CreatePreviewSurface(winrt::Windows::UI::Composition::Compositor const& compositor);

private:
    VideoRecordingSession(
        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& device,
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& item,
        winrt::Windows::Graphics::SizeInt32 const& resolution,
        uint32_t bitRate,
        uint32_t frameRate,
        winrt::Windows::Storage::Streams::IRandomAccessStream const& stream);
    void CloseInternal();

    void OnMediaStreamSourceStarting(
        winrt::Windows::Media::Core::MediaStreamSource const& sender,
        winrt::Windows::Media::Core::MediaStreamSourceStartingEventArgs const& args);
    void OnMediaStreamSourceSampleRequested(
        winrt::Windows::Media::Core::MediaStreamSource const& sender,
        winrt::Windows::Media::Core::MediaStreamSourceSampleRequestedEventArgs const& args);

    void OnAudioQuantumStarted(
        winrt::Windows::Media::Audio::AudioGraph const& sender,
        winrt::Windows::Foundation::IInspectable const& args);

private:
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_device{ nullptr };
    winrt::com_ptr<ID3D11Device> m_d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> m_d3dContext;

    winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem::Closed_revoker m_itemClosed;
    std::shared_ptr<CaptureFrameGenerator> m_frameGenerator;

    winrt::Windows::Storage::Streams::IRandomAccessStream m_stream{ nullptr };
    winrt::Windows::Media::MediaProperties::MediaEncodingProfile m_encodingProfile{ nullptr };
    winrt::Windows::Media::Core::VideoStreamDescriptor m_videoDescriptor{ nullptr };
    winrt::Windows::Media::Core::MediaStreamSource m_streamSource{ nullptr };
    winrt::Windows::Media::Transcoding::MediaTranscoder m_transcoder{ nullptr };

    winrt::Windows::Media::Audio::AudioGraph m_audioGraph{ nullptr };
    winrt::Windows::Media::Audio::AudioDeviceInputNode m_audioInputNode{ nullptr };
    winrt::Windows::Media::Audio::AudioFrameOutputNode m_audioOutputNode{ nullptr };
    wil::srwlock m_audioLock;
    wil::unique_event m_audioEvent;
    std::deque<winrt::Windows::Media::Core::MediaStreamSample> m_audioSamples;
    bool m_ended = false;

    winrt::com_ptr<IDXGISwapChain1> m_previewSwapChain;
    winrt::com_ptr<ID3D11RenderTargetView> m_renderTargetView;

    std::atomic<bool> m_isRecording = false;
    std::atomic<bool> m_closed = false;
};