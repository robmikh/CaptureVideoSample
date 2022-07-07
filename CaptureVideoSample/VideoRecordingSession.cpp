#include "pch.h"
#include "VideoRecordingSession.h"
#include "CaptureFrameGenerator.h"

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Foundation::Numerics;
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

winrt::SizeInt32 EnsureEvenSize(winrt::SizeInt32 const size)
{
    return winrt::SizeInt32{ EnsureEven(size.Width), EnsureEven(size.Height) };
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
    auto inputSize = EnsureEvenSize(itemSize);
    auto outputSize = EnsureEvenSize(resolution);

    // Create VideoProcessor
    m_videoProcessor = std::make_shared<VideoProcessor>(
        m_d3dDevice,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        inputSize,
        DXGI_FORMAT_NV12,
        outputSize);

    // Create VideoEncoder
    m_videoEncoder = std::make_shared<VideoEncoder>(
        encoderDevice, 
        m_d3dDevice,
        outputSize, // We're scaling in the video processor
        outputSize,
        bitRate, 
        frameRate);
    m_videoEncoder->SetSampleRequestedCallback(std::bind(&VideoRecordingSession::OnSampleRequested, this));
    m_videoEncoder->SetSampleRenderedCallback(std::bind(&VideoRecordingSession::OnSampleRendered, this, std::placeholders::_1));

    // Setup capture
    m_frameGenerator = std::make_shared<CaptureFrameGenerator>(m_device, m_item, inputSize);
    auto weakPointer{ std::weak_ptr{ m_frameGenerator } };
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
        static_cast<uint32_t>(inputSize.Width),
        static_cast<uint32_t>(inputSize.Height),
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
            m_frameGenerator->StopCapture();
        }
    }
}

void VideoRecordingSession::CloseInternal()
{
    m_frameGenerator->StopCapture();
    m_itemClosed.revoke();
}

std::optional<std::unique_ptr<InputSample>> VideoRecordingSession::OnSampleRequested()
{
    if (auto frameOpt = m_frameGenerator->TryGetNextFrame())
    {
        try
        {
            auto frame = frameOpt.value();

            if (!m_seenFirstTimeStamp)
            {
                m_firstTimeStamp = frame.SystemRelativeTime();
                m_seenFirstTimeStamp = true;
            }
            auto timeStamp = frame.SystemRelativeTime() - m_firstTimeStamp;
            auto contentSize = frame.ContentSize();
            auto frameTexture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
            D3D11_TEXTURE2D_DESC desc = {};
            frameTexture->GetDesc(&desc);

            m_outstandingFrames.insert({ timeStamp, frame });

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

            // Process our back buffer
            m_videoProcessor->ProcessTexture(backBuffer);

            // Present the preview
            DXGI_PRESENT_PARAMETERS presentParameters{};
            winrt::check_hresult(m_previewSwapChain->Present1(0, 0, &presentParameters));

            // Get our NV12 texture
            auto videoOutputTexture = m_videoProcessor->OutputTexture();

            // Make a copy for the sample
            desc = {};
            videoOutputTexture->GetDesc(&desc);
            winrt::com_ptr<ID3D11Texture2D> sampleTexture;
            winrt::check_hresult(m_d3dDevice->CreateTexture2D(&desc, nullptr, sampleTexture.put()));
            m_d3dContext->CopyResource(sampleTexture.get(), videoOutputTexture.get());

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

    // Retire the frame
    int64_t time = 0;
    winrt::check_hresult(mfSample->GetSampleTime(&time));
    auto timeStamp = winrt::TimeSpan{ time };
    winrt::Direct3D11CaptureFrame frame = m_outstandingFrames.at(timeStamp);
    m_outstandingFrames.erase(timeStamp);
    frame.Close();

    winrt::check_hresult(m_sinkWriter->WriteSample(m_sinkWriterStreamIndex, mfSample.get()));
}

winrt::ICompositionSurface VideoRecordingSession::CreatePreviewSurface(winrt::Compositor const& compositor)
{
    return util::CreateCompositionSurfaceForSwapChain(compositor, m_previewSwapChain.get());
}
