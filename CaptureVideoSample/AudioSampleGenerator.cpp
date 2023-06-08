#include "pch.h"
#include "AudioSampleGenerator.h"

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Storage;
    using namespace Windows::Storage::Streams;
    using namespace Windows::Media;
    using namespace Windows::Media::Audio;
    using namespace Windows::Media::Capture;
    using namespace Windows::Media::Core;
    using namespace Windows::Media::Render;
    using namespace Windows::Media::MediaProperties;
    using namespace CaptureVideoSample;
}

AudioSampleGenerator::AudioSampleGenerator()
{
    m_audioEvent.create(wil::EventOptions::ManualReset);
    m_endEvent.create(wil::EventOptions::ManualReset);
}

AudioSampleGenerator::~AudioSampleGenerator()
{
    m_audioGraph.Stop();
    m_audioGraph.Close();
}

winrt::IAsyncAction AudioSampleGenerator::InitializeAsync()
{
    auto expected = false;
    if (m_initialized.compare_exchange_strong(expected, true))
    {
        // Initialize the audio graph
        auto audioGraphSettings = winrt::AudioGraphSettings(winrt::AudioRenderCategory::Media);
        auto audioGraphResult = co_await winrt::AudioGraph::CreateAsync(audioGraphSettings);
        if (audioGraphResult.Status() != winrt::AudioGraphCreationStatus::Success)
        {
            throw winrt::hresult_error(E_FAIL, L"Failed to initialize AudioGraph!");
        }
        m_audioGraph = audioGraphResult.Graph();

        // Initialize audio input and output nodes
        auto inputNodeResult = co_await m_audioGraph.CreateDeviceInputNodeAsync(winrt::MediaCategory::Media);
        if (inputNodeResult.Status() != winrt::AudioDeviceNodeCreationStatus::Success)
        {
            throw winrt::hresult_error(E_FAIL, L"Failed to initialize input audio node!");
        }
        m_microphoneNode = inputNodeResult.DeviceInputNode();

        m_loopbackNode = co_await winrt::AudioLoopbackInputNode::CreateLoopbackNodeExcludingProcessAsync(m_audioGraph, GetCurrentProcessId(), winrt::AudioEncodingProperties::CreatePcm(44100, 2, 16));

        m_submixNode = m_audioGraph.CreateSubmixNode();

        m_audioOutputNode = m_audioGraph.CreateFrameOutputNode();

        // Hookup audio nodes
        m_microphoneNode.AddOutgoingConnection(m_submixNode, 0.5);
        m_loopbackNode.AudioGraphNode().AddOutgoingConnection(m_submixNode, 0.5);
        m_submixNode.AddOutgoingConnection(m_audioOutputNode);
        m_audioGraph.QuantumStarted({ this, &AudioSampleGenerator::OnAudioQuantumStarted });
    }
}

winrt::AudioEncodingProperties AudioSampleGenerator::GetEncodingProperties()
{
    CheckInitialized();
    return m_audioOutputNode.EncodingProperties();
}

std::optional<winrt::MediaStreamSample> AudioSampleGenerator::TryGetNextSample()
{
    CheckInitialized();
    CheckStarted();

    {
        auto lock = m_lock.lock_exclusive();
        if (m_samples.empty() && m_endEvent.is_signaled())
        {
            return std::nullopt;
        }
        else if (!m_samples.empty())
        {
            std::optional result(m_samples.front());
            m_samples.pop_front();
            return result;
        }
    }

    m_audioEvent.ResetEvent();
    std::vector<HANDLE> events = { m_endEvent.get(), m_audioEvent.get() };
    auto waitResult = WaitForMultipleObjectsEx(static_cast<DWORD>(events.size()), events.data(), false, INFINITE, false);
    auto eventIndex = -1;
    switch (waitResult)
    {
    case WAIT_OBJECT_0:
    case WAIT_OBJECT_0 + 1:
        eventIndex = waitResult - WAIT_OBJECT_0;
        break;
    }
    WINRT_VERIFY(eventIndex >= 0);

    auto signaledEvent = events[eventIndex];
    if (signaledEvent == m_endEvent.get())
    {
        return std::nullopt;
    }
    else
    {
        auto lock = m_lock.lock_exclusive();
        std::optional result(m_samples.front());
        m_samples.pop_front();
        return result;
    }
}

void AudioSampleGenerator::Start()
{
    CheckInitialized();
    auto expected = false;
    if (m_started.compare_exchange_strong(expected, true))
    {
        m_audioGraph.Start();
        m_loopbackNode.StartCapture();
    }
}

void AudioSampleGenerator::Stop()
{
    CheckInitialized();
    if (m_started.load())
    {
        m_audioGraph.Stop();
        m_loopbackNode.Close();
        m_endEvent.SetEvent();
    }
}

void AudioSampleGenerator::OnAudioQuantumStarted(winrt::AudioGraph const&, winrt::IInspectable const&)
{
    {
        auto lock = m_lock.lock_exclusive();

        auto frame = m_audioOutputNode.GetFrame();
        std::optional<winrt::TimeSpan> timestamp = frame.RelativeTime();
        auto audioBuffer = frame.LockBuffer(winrt::AudioBufferAccessMode::Read);

        auto sampleBuffer = winrt::Buffer::CreateCopyFromMemoryBuffer(audioBuffer);
        sampleBuffer.Length(audioBuffer.Length());
        auto sample = winrt::MediaStreamSample::CreateFromBuffer(sampleBuffer, timestamp.value());
        m_samples.push_back(sample);
    }
    m_audioEvent.SetEvent();
}