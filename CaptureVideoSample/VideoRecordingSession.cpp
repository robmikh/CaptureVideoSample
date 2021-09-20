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
    using namespace Windows::Storage::Streams;
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
    std::shared_ptr<VideoEncoderDevice> const& encoderDevice,
    winrt::SizeInt32 const& resolution, 
    uint32_t bitRate, 
    uint32_t frameRate, 
    winrt::Windows::Storage::Streams::IRandomAccessStream const& stream)
{
    m_device = device;
    m_d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(m_device);
    m_d3dDevice->GetImmediateContext(m_d3dContext.put());

    m_item = item;
    auto itemSize = item.Size();
    auto inputWidth = itemSize.Width;
    auto inputHeight = itemSize.Height;
    auto outputWidth = EnsureEven(resolution.Width);
    auto outputHeight = EnsureEven(resolution.Height);

    // TODO: Fix sizing
    WINRT_VERIFY(inputWidth == outputWidth);
    WINRT_VERIFY(inputHeight == outputHeight);

    // Setup video conversion
    m_videoDevice = m_d3dDevice.as<ID3D11VideoDevice>();
    m_videoContext = m_d3dContext.as<ID3D11VideoContext>();

    winrt::com_ptr<ID3D11VideoProcessorEnumerator> videoEnum;
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC videoDesc = {};
    videoDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    videoDesc.InputFrameRate.Numerator = 60;
    videoDesc.InputFrameRate.Denominator = 1;
    videoDesc.InputWidth = outputWidth;
    videoDesc.InputHeight = outputHeight;
    videoDesc.OutputFrameRate.Numerator = 60;
    videoDesc.OutputFrameRate.Denominator = 1;
    videoDesc.OutputWidth = itemSize.Width;
    videoDesc.OutputHeight = itemSize.Height;
    videoDesc.Usage = D3D11_VIDEO_USAGE_OPTIMAL_QUALITY;
    winrt::check_hresult(m_videoDevice->CreateVideoProcessorEnumerator(&videoDesc, videoEnum.put()));

    winrt::check_hresult(m_videoDevice->CreateVideoProcessor(videoEnum.get(), 0, m_videoProcessor.put()));

    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = outputWidth;
    textureDesc.Height = outputHeight;
    textureDesc.ArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_NV12;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_VIDEO_ENCODER;
    winrt::check_hresult(m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, m_videoOutputTexture.put()));

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc = {};
    outputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    outputViewDesc.Texture2D.MipSlice = 0;
    winrt::check_hresult(m_videoDevice->CreateVideoProcessorOutputView(m_videoOutputTexture.get(), videoEnum.get(), &outputViewDesc, m_videoOutput.put()));

    textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    winrt::check_hresult(m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, m_videoInputTexture.put()));

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc = {};
    inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inputViewDesc.Texture2D.MipSlice = 0;
    // The output of the scene renderer is the input to the video conversion
    winrt::check_hresult(m_videoDevice->CreateVideoProcessorInputView(m_videoInputTexture.get(), videoEnum.get(), &inputViewDesc, m_videoInput.put()));

    // Create VideoEncoder
    m_videoEncoder = std::make_shared<VideoEncoder>(
        encoderDevice, 
        m_d3dDevice, 
        winrt::SizeInt32{ outputWidth, outputHeight }, 
        bitRate, 
        frameRate);
    m_videoEncoder->SetSampleRequestedCallback(std::bind(&VideoRecordingSession::OnSampleRequested, this));
    m_videoEncoder->SetSampleRenderedCallback(std::bind(&VideoRecordingSession::OnSampleRendered, this, std::placeholders::_1));

    // Setup capture
    m_frameWait = std::make_shared<CaptureFrameWait>(m_device, m_item);
    auto weakPointer{ std::weak_ptr{ m_frameWait } };
    m_itemClosed = item.Closed(winrt::auto_revoke, [weakPointer](auto&, auto&)
    {
        auto sharedPointer{ weakPointer.lock() };

        if (sharedPointer)
        {
            sharedPointer->StopCapture();
        }
    });

    // Setup MFSinkWriter
    m_stream = stream;
    winrt::com_ptr<IMFByteStream> byteStream;
    winrt::check_hresult(MFCreateMFByteStreamOnStreamEx(stream.as<::IUnknown>().get(), byteStream.put()));
    winrt::check_hresult(MFCreateSinkWriterFromURL(L".mp4", byteStream.get(), nullptr, m_sinkWriter.put()));
    winrt::check_hresult(m_sinkWriter->AddStream(m_videoEncoder->OutputType().get(), &m_sinkWriterStreamIndex));
    winrt::check_hresult(m_sinkWriter->SetInputMediaType(m_sinkWriterStreamIndex, m_videoEncoder->OutputType().get(), nullptr));
    winrt::TimeSpan frameDuration = std::chrono::milliseconds((int)((1.0 / (float)frameRate) * 1000.0));
    m_frameDuration = frameDuration.count();

    // Setup preview
    m_previewSwapChain = util::CreateDXGISwapChain(
        m_d3dDevice, 
        static_cast<uint32_t>(outputWidth),
        static_cast<uint32_t>(outputHeight),
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
        // We need to hold a reference to ourselves so that we have enough
        // time to finish the encoding even when the caller drops their 
        // last reference.
        auto thisRef = shared_from_this();

        auto sinkWriter = m_sinkWriter;
        winrt::check_hresult(sinkWriter->BeginWriting());
        co_await m_videoEncoder->StartAsync();
        winrt::check_hresult(sinkWriter->Finalize());
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
    m_itemClosed.revoke();
}

std::optional<std::unique_ptr<InputSample>> VideoRecordingSession::OnSampleRequested()
{
    if (auto frame = m_frameWait->TryGetNextFrame())
    {
        try
        {
            if (!m_seenFirstTimeStamp)
            {
                m_firstTimeStamp = frame->SystemRelativeTime;
                m_seenFirstTimeStamp = true;
            }
            auto timeStamp = frame->SystemRelativeTime - m_firstTimeStamp;
            auto contentSize = frame->ContentSize;
            auto frameTexture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame->FrameTexture);
            D3D11_TEXTURE2D_DESC desc = {};
            frameTexture->GetDesc(&desc);

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

            // Copy the back buffer to the video input texture
            m_d3dContext->CopyResource(m_videoInputTexture.get(), backBuffer.get());

            // Present the preview
            DXGI_PRESENT_PARAMETERS presentParameters{};
            winrt::check_hresult(m_previewSwapChain->Present1(0, 0, &presentParameters));

            // Convert to NV12
            D3D11_VIDEO_PROCESSOR_STREAM videoStream = {};
            videoStream.Enable = true;
            videoStream.OutputIndex = 0;
            videoStream.InputFrameOrField = 0;
            videoStream.pInputSurface = m_videoInput.get();
            winrt::check_hresult(m_videoContext->VideoProcessorBlt(m_videoProcessor.get(), m_videoOutput.get(), 0, 1, &videoStream));

            // Make a copy for the sample
            desc = {};
            m_videoOutputTexture->GetDesc(&desc);
            winrt::com_ptr<ID3D11Texture2D> sampleTexture;
            winrt::check_hresult(m_d3dDevice->CreateTexture2D(&desc, nullptr, sampleTexture.put()));
            m_d3dContext->CopyResource(sampleTexture.get(), m_videoOutputTexture.get());

            auto sample = std::make_unique<InputSample>(std::move(InputSample{ timeStamp, sampleTexture }));
            return std::optional<std::unique_ptr<InputSample>>{ std::move(sample) };
        }
        catch (winrt::hresult_error const& error)
        {
            OutputDebugStringW(error.message().c_str());
            CloseInternal();
            return std::nullopt;
        }
    }
    else
    {
        CloseInternal();
        return std::nullopt;
    }
}

void VideoRecordingSession::OnSampleRendered(std::unique_ptr<OutputSample> sample)
{
    auto mfSample = sample->MFSample;
    winrt::check_hresult(m_sinkWriter->WriteSample(m_sinkWriterStreamIndex, mfSample.get()));
}

winrt::ICompositionSurface VideoRecordingSession::CreatePreviewSurface(winrt::Compositor const& compositor)
{
    return util::CreateCompositionSurfaceForSwapChain(compositor, m_previewSwapChain.get());
}
