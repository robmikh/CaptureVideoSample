#include "pch.h"
#include "VideoRecordingSession.h"
#include "CaptureFrameWait.h"

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Graphics;
    using namespace Windows::Graphics::Capture;
    using namespace Windows::Graphics::DirectX;
    using namespace Windows::Graphics::DirectX::Direct3D11;
    using namespace Windows::Storage;
    using namespace Windows::UI::Composition;
    using namespace Windows::Media::Core;
    using namespace Windows::Media::Transcoding;
    using namespace Windows::Media::MediaProperties;
}

namespace util
{
    using namespace robmikh::common::uwp;
}

const float CLEARCOLOR[] = { 0.0f, 0.0f, 0.0f, 1.0f };

int32_t EnsureEven(int32_t value)
{
    if (value % 2 == 0)
    {
        return value;
    }
    else
    {
        return value + 1;
    }
}

VideoRecordingSession::VideoRecordingSession(
    winrt::IDirect3DDevice const& device, 
    winrt::GraphicsCaptureItem const& item, 
    winrt::SizeInt32 const& resolution, 
    uint32_t bitRate, 
    uint32_t frameRate, 
    winrt::Windows::Storage::Streams::IRandomAccessStream const& stream)
{
    m_device = device;
    m_d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(m_device);
    m_d3dDevice->GetImmediateContext(m_d3dContext.put());

    m_item = item;
    m_frameWait = std::make_unique<CaptureFrameWait>(m_device, m_item);

    auto width = EnsureEven(resolution.Width);
    auto height = EnsureEven(resolution.Height);

    m_encodingProfile = winrt::MediaEncodingProfile();
    m_encodingProfile.Container().Subtype(L"MPEG4");
    auto video = m_encodingProfile.Video();
    video.Subtype(L"H264");
    video.Width(width);
    video.Height(height);
    video.Bitrate(bitRate);
    video.FrameRate().Numerator(frameRate);
    video.FrameRate().Denominator(1);
    video.PixelAspectRatio().Numerator(1);
    video.PixelAspectRatio().Denominator(1);
    m_encodingProfile.Video(video);

    m_stream = stream;

    m_previewSwapChain = util::CreateDXGISwapChain(
        m_d3dDevice, 
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        DXGI_FORMAT_B8G8R8A8_UNORM, 
        2);
    winrt::com_ptr<ID3D11Texture2D> backBuffer;
    winrt::check_hresult(m_previewSwapChain->GetBuffer(0, winrt::guid_of<ID3D11Texture2D>(), backBuffer.put_void()));
    winrt::check_hresult(m_d3dDevice->CreateRenderTargetView(backBuffer.get(), nullptr, m_renderTargetView.put()));
}

VideoRecordingSession::~VideoRecordingSession()
{
    Close();
}

winrt::IAsyncAction VideoRecordingSession::StartAsync()
{
    auto expected = false;
    if (m_isRecording.compare_exchange_strong(expected, true))
    {
        auto itemSize = m_item.Size();
        auto width = itemSize.Width;
        auto height = itemSize.Height;

        // Describe our input: uncompressed BGRA8 buffers
        auto properties = winrt::VideoEncodingProperties::CreateUncompressed(
            winrt::MediaEncodingSubtypes::Bgra8(),
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height));
        m_videoDescriptor = winrt::VideoStreamDescriptor(properties);

        // Create our MediaStreamSource
        m_streamSource = winrt::MediaStreamSource(m_videoDescriptor);
        m_streamSource.BufferTime(std::chrono::seconds(0));
        m_streamSource.Starting({ this, &VideoRecordingSession::OnMediaStreamSourceStarting });
        m_streamSource.SampleRequested({ this, &VideoRecordingSession::OnMediaStreamSourceSampleRequested });

        // Create our transcoder
        m_transcoder = winrt::MediaTranscoder();
        m_transcoder.HardwareAccelerationEnabled(true);

        // Start encoding
        auto transcode = co_await m_transcoder.PrepareMediaStreamSourceTranscodeAsync(m_streamSource, m_stream, m_encodingProfile);
        co_await transcode.TranscodeAsync();
    }
    co_return;
}

void VideoRecordingSession::Close()
{
    auto expected = false;
    if (m_closed.compare_exchange_strong(expected, true))
    {
        expected = true;
        if (!m_isRecording.compare_exchange_strong(expected, false))
        {
            CloseInternal();
        }
        else
        {
            m_frameWait->StopCapture();
        }
    }
}

void VideoRecordingSession::CloseInternal()
{
    m_frameWait->StopCapture();
}

void VideoRecordingSession::OnMediaStreamSourceStarting(
    winrt::MediaStreamSource const& sender, 
    winrt::MediaStreamSourceStartingEventArgs const& args)
{
    auto frame = *m_frameWait->TryGetNextFrame();
    args.Request().SetActualStartPosition(frame.SystemRelativeTime);
}

void VideoRecordingSession::OnMediaStreamSourceSampleRequested(
    winrt::MediaStreamSource const& sender, 
    winrt::MediaStreamSourceSampleRequestedEventArgs const& args)
{
    auto request = args.Request();
    if (auto frame = m_frameWait->TryGetNextFrame())
    {
        try
        {
            auto timeStamp = frame->SystemRelativeTime;
            auto contentSize = frame->ContentSize;
            auto frameTexture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame->FrameTexture);
            D3D11_TEXTURE2D_DESC desc = {};
            frameTexture->GetDesc(&desc);

            // TODO: Update preview
            winrt::com_ptr<ID3D11Texture2D> backBuffer;
            winrt::check_hresult(m_previewSwapChain->GetBuffer(0, winrt::guid_of<ID3D11Texture2D>(), backBuffer.put_void()));

            // In order to support window resizing, we need to only copy out the part of
            // the buffer that contains the window. If the window is smaller than the buffer,
            // then it's a straight forward copy using the ContentSize. If the window is larger,
            // we need to clamp to the size of the buffer. For simplicity, we always clamp.
            auto width = std::clamp(contentSize.Width, 0, static_cast<int32_t>(desc.Width));
            auto height = std::clamp(contentSize.Height, 0, static_cast<int32_t>(desc.Height));

            D3D11_BOX region = {};
            region.left = 0;
            region.right = width;
            region.top = 0;
            region.bottom = height;
            region.back = 1;

            m_d3dContext->ClearRenderTargetView(m_renderTargetView.get(), CLEARCOLOR);
            m_d3dContext->CopySubresourceRegion(
                backBuffer.get(),
                0,
                0, 0, 0,
                frameTexture.get(),
                0,
                &region);

            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            desc.CPUAccessFlags = 0;
            desc.MiscFlags = 0;
            winrt::com_ptr<ID3D11Texture2D> sampleTexture;
            winrt::check_hresult(m_d3dDevice->CreateTexture2D(&desc, nullptr, sampleTexture.put()));
            m_d3dContext->CopyResource(sampleTexture.get(), backBuffer.get());
            auto dxgiSurface = sampleTexture.as<IDXGISurface>();
            auto sampleSurface = CreateDirect3DSurface(dxgiSurface.get());

            DXGI_PRESENT_PARAMETERS presentParameters{};
            winrt::check_hresult(m_previewSwapChain->Present1(0, 0, &presentParameters));

            auto sample = winrt::MediaStreamSample::CreateFromDirect3D11Surface(sampleSurface, timeStamp);
            request.Sample(sample);
        }
        catch (winrt::hresult_error const& error)
        {
            OutputDebugStringW(error.message().c_str());
            request.Sample(nullptr);
            CloseInternal();
            return;
        }
    }
    else
    {
        request.Sample(nullptr);
        CloseInternal();
    }
}

winrt::ICompositionSurface VideoRecordingSession::CreatePreviewSurface(winrt::Compositor const& compositor)
{
    return util::CreateCompositionSurfaceForSwapChain(compositor, m_previewSwapChain.get());
}
