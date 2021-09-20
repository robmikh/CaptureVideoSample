#include "pch.h"
#include "VideoEncoder.h"
#include "VideoEncoderDevice.h"

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Graphics;
}

VideoEncoder::VideoEncoder(
    std::shared_ptr<VideoEncoderDevice> const& encoderDevice, 
    winrt::com_ptr<ID3D11Device> const& d3dDevice,
    winrt::SizeInt32 const& inputResolution,
    winrt::SizeInt32 const& outputResolution,
    uint32_t bitRate,
    uint32_t frameRate)
{
    m_transform = encoderDevice->CreateTransform();
    m_d3dDevice = d3dDevice;

    // Create MF device manager
    winrt::check_hresult(MFCreateDXGIDeviceManager(&m_deviceManagerResetToken, m_mediaDeviceManager.put()));
    winrt::check_hresult(m_mediaDeviceManager->ResetDevice(m_d3dDevice.get(), m_deviceManagerResetToken));

    // Setup MFTransform
    m_eventGenerator = m_transform.as<IMFMediaEventGenerator>();
    winrt::com_ptr<IMFAttributes> attributes;
    winrt::check_hresult(m_transform->GetAttributes(attributes.put()));
    winrt::check_hresult(attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, 1));
    winrt::check_hresult(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, 1));

    DWORD numInputStreams = 0;
    DWORD numOutputStreams = 0;
    winrt::check_hresult(m_transform->GetStreamCount(&numInputStreams, &numOutputStreams));
    std::vector<DWORD> inputStreamIds(numInputStreams, 0);
    std::vector<DWORD> outputSteamIds(numOutputStreams, 0);
    {
        auto hr = m_transform->GetStreamIDs(numInputStreams, inputStreamIds.data(), numOutputStreams, outputSteamIds.data());
        // https://docs.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-getstreamids
        // This method can return E_NOTIMPL if both of the following conditions are true:
        //   * The transform has a fixed number of streams.
        //   * The streams are numbered consecutively from 0 to n – 1, where n is the
        //     number of input streams or output streams. In other words, the first 
        //     input stream is 0, the second is 1, and so on; and the first output 
        //     stream is 0, the second is 1, and so on. 
        if (hr == E_NOTIMPL)
        {
            for (auto i = 0; i < inputStreamIds.size(); i++)
            {
                inputStreamIds[i] = i;
            }
            for (auto i = 0; i < outputSteamIds.size(); i++)
            {
                outputSteamIds[i] = i;
            }
        }
        else
        {
            winrt::check_hresult(hr);
        }
    }
    m_inputStreamId = inputStreamIds[0];
    m_outputStreamId = outputSteamIds[0];

    winrt::check_hresult(m_transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(m_mediaDeviceManager.get())));

    MFT_REGISTER_TYPE_INFO info = { MFMediaType_Video, MFVideoFormat_H264 };
    winrt::com_ptr<IMFMediaType> outputType;
    winrt::check_hresult(MFCreateMediaType(outputType.put()));
    winrt::check_hresult(outputType->SetGUID(MF_MT_MAJOR_TYPE, info.guidMajorType));
    winrt::check_hresult(outputType->SetGUID(MF_MT_SUBTYPE, info.guidSubtype));
    winrt::check_hresult(outputType->SetUINT32(MF_MT_AVG_BITRATE, bitRate));
    winrt::check_hresult(MFSetAttributeSize(outputType.get(), MF_MT_FRAME_SIZE, outputResolution.Width, outputResolution.Height));
    winrt::check_hresult(MFSetAttributeRatio(outputType.get(), MF_MT_FRAME_RATE, frameRate, 1));
    winrt::check_hresult(MFSetAttributeRatio(outputType.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
    winrt::check_hresult(outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    winrt::check_hresult(outputType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, 1));
    winrt::check_hresult(m_transform->SetOutputType(m_outputStreamId, outputType.get(), 0));
    m_outputType = outputType;

    winrt::com_ptr<IMFMediaType> inputType;
    {
        auto count = 0;
        for (count = 0;; count++)
        {
            inputType = nullptr;
            auto hr = m_transform->GetInputAvailableType(m_inputStreamId, count, inputType.put());
            if (hr == MF_E_NO_MORE_TYPES)
            {
                break;
            }
            winrt::check_hresult(hr);
            winrt::check_hresult(inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
            winrt::check_hresult(inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
            winrt::check_hresult(MFSetAttributeSize(inputType.get(), MF_MT_FRAME_SIZE, inputResolution.Width, inputResolution.Height));
            winrt::check_hresult(MFSetAttributeRatio(inputType.get(), MF_MT_FRAME_RATE, 60, 1));
            hr = m_transform->SetInputType(m_inputStreamId, inputType.get(), MFT_SET_TYPE_TEST_ONLY);
            if (hr == MF_E_INVALIDMEDIATYPE)
            {
                continue;
            }
            winrt::check_hresult(hr);
            break;
        }
        if (inputType == nullptr)
        {
            throw std::runtime_error("No suitable input type found");
        }
        winrt::check_hresult(m_transform->SetInputType(m_inputStreamId, inputType.get(), 0));
    }
}

winrt::Windows::Foundation::IAsyncAction VideoEncoder::StartAsync()
{
    bool expected = false;
    if (m_started.compare_exchange_strong(expected, true))
    {
        // Callbacks must both be set
        if (m_sampleRequestedCallback == nullptr || m_sampleRenderedCallback == nullptr)
        {
            throw std::runtime_error("Sample requested and rendered callbacks must be set before starting");
        }

        // Start a seperate thread to drive the transform
        m_encoderCompletedEvent = wil::shared_event(wil::EventOptions::None);
        auto transformThread = std::thread([=]()
            {
                winrt::check_hresult(MFStartup(MF_VERSION));
                Encode();
                m_encoderCompletedEvent.SetEvent();
            });
        co_await winrt::resume_on_signal(m_encoderCompletedEvent.get());
        transformThread.join(); // This should return immediately
    }
    co_return;
}

void VideoEncoder::Encode()
{
    winrt::check_hresult(m_transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0));
    winrt::check_hresult(m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
    winrt::check_hresult(m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));

    bool shouldExit = false;
    while (!shouldExit)
    {
        winrt::com_ptr<IMFMediaEvent> event;
        m_eventGenerator->GetEvent(0, event.put());

        MediaEventType eventType;
        winrt::check_hresult(event->GetType(&eventType));

        switch (eventType)
        {
        case METransformNeedInput:
            shouldExit = OnTransformInputRequested();
            break;
        case METransformHaveOutput:
            OnTransformOutputReady();
            break;
        default:
            throw std::runtime_error("Unknown media event type");
        }
    }

    winrt::check_hresult(m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0));
    winrt::check_hresult(m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0));
    winrt::check_hresult(m_transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0));
}

bool VideoEncoder::OnTransformInputRequested()
{
    bool shouldExit = true;
    if (auto sample = m_sampleRequestedCallback())
    {
        winrt::com_ptr<IMFMediaBuffer> inputBuffer;
        winrt::check_hresult(MFCreateDXGISurfaceBuffer(winrt::guid_of<ID3D11Texture2D>(), sample->get()->Texture.get(), 0, false, inputBuffer.put()));
        winrt::com_ptr<IMFSample> mfSample;
        winrt::check_hresult(MFCreateSample(mfSample.put()));
        winrt::check_hresult(mfSample->AddBuffer(inputBuffer.get()));
        winrt::check_hresult(mfSample->SetSampleTime(sample->get()->TimeStamp.count()));

        winrt::check_hresult(m_transform->ProcessInput(m_inputStreamId, mfSample.get(), 0));
        shouldExit = false;
    }
    return shouldExit;
}

void VideoEncoder::OnTransformOutputReady()
{
    DWORD status = 0;
    MFT_OUTPUT_DATA_BUFFER outputBuffer = {};
    outputBuffer.dwStreamID = m_outputStreamId;

    winrt::check_hresult(m_transform->ProcessOutput(0, 1, &outputBuffer, &status));
    winrt::com_ptr<IMFSample> sample;
    sample.attach(outputBuffer.pSample);
    winrt::com_ptr<IMFCollection> events;
    events.attach(outputBuffer.pEvents);

    auto outputSample = std::make_unique<OutputSample>(std::move(OutputSample{ sample }));
    m_sampleRenderedCallback(std::move(outputSample));
}
