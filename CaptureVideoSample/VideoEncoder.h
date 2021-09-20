#pragma once

class VideoEncoderDevice;

struct InputSample
{
    winrt::Windows::Foundation::TimeSpan TimeStamp;
    winrt::com_ptr<ID3D11Texture2D> Texture;
};

struct OutputSample
{
    winrt::com_ptr<IMFSample> MFSample;
};

class VideoEncoder
{
public:
    VideoEncoder(
        std::shared_ptr<VideoEncoderDevice> const& encoderDevice, 
        winrt::com_ptr<ID3D11Device> const& d3dDevice,
        winrt::Windows::Graphics::SizeInt32 const& resolution,
        uint32_t bitRate,
        uint32_t frameRate);

    winrt::com_ptr<IMFMediaType> const& OutputType() { return m_outputType; }
    void SetSampleRequestedCallback(std::function<std::optional<std::unique_ptr<InputSample>>()> sampleRequestedCallback)
    {
        CheckIfStarted();
        m_sampleRequestedCallback = sampleRequestedCallback;
    }
    void SetSampleRenderedCallback(std::function<void(std::unique_ptr<OutputSample>)> sampleRenderedCallback)
    {
        CheckIfStarted();
        m_sampleRenderedCallback = sampleRenderedCallback;
    }

    winrt::Windows::Foundation::IAsyncAction StartAsync();

private:
    void CheckIfStarted()
    {
        if (m_started.load())
        {
            throw std::runtime_error("It is invalid to call this function after starting the encoder.");
        }
    }

    void Encode();
    bool OnTransformInputRequested();
    void OnTransformOutputReady();

private:
    winrt::com_ptr<ID3D11Device> m_d3dDevice;
    winrt::com_ptr<IMFDXGIDeviceManager> m_mediaDeviceManager;
    uint32_t m_deviceManagerResetToken = 0;

    winrt::com_ptr<IMFTransform> m_transform;
    winrt::com_ptr<IMFMediaEventGenerator> m_eventGenerator;
    DWORD m_inputStreamId = 0;
    DWORD m_outputStreamId = 0;
    winrt::com_ptr<IMFMediaType> m_outputType;

    std::function<std::optional<std::unique_ptr<InputSample>>()> m_sampleRequestedCallback;
    std::function<void(std::unique_ptr<OutputSample>)> m_sampleRenderedCallback;

    std::atomic<bool> m_started = false;
    wil::shared_event m_encoderCompletedEvent{ nullptr };
};