#include "pch.h"
#include "AudioLoopbackInputNode.h"
#include "AudioLoopbackInputNode.g.cpp"

#define BITS_PER_BYTE 8

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Media;
    using namespace Windows::Media::Audio;
    using namespace Windows::Media::MediaProperties;
}

namespace api
{
    using namespace ::winrt::CaptureVideoSample;
}

namespace winrt::CaptureVideoSample::implementation
{
    AudioLoopbackInputNode::AudioLoopbackInputNode(winrt::AudioGraph const& graph, winrt::AudioEncodingProperties const& encodingProperties)
    {
        if (encodingProperties.Subtype() != L"PCM")
        {
            throw winrt::hresult_error(E_INVALIDARG, L"Uncompressed audio only.");
        }
        m_node = graph.CreateFrameInputNode(encodingProperties);
        m_sampleRate = encodingProperties.SampleRate();

        // Create events for sample ready or user stop
        m_sampleReadyEvent.create(wil::EventOptions::None);

        // Register MMCSS work queue
        DWORD taskId = 0;
        winrt::check_hresult(MFLockSharedWorkQueue(L"Capture", 0, &taskId, &m_queueID));

        // Set the capture event work queue to use the MMCSS queue
        m_xSampleReady.SetQueueID(m_queueID);

        // Create the completion event as auto-reset
        m_activateCompleted.create(wil::EventOptions::None);

        // Create the capture-stopped event as auto-reset
        m_captureStopped.create(wil::EventOptions::None);
    }
    winrt::IAsyncOperation<api::AudioLoopbackInputNode> AudioLoopbackInputNode::CreateLoopbackNodeIncludingProcessAsync(winrt::AudioGraph graph, uint32_t processId, winrt::AudioEncodingProperties encodingProperties)
    {
        return CreateLoopbackNodeAsync(graph, processId, encodingProperties, true);
    }
    winrt::IAsyncOperation<api::AudioLoopbackInputNode> AudioLoopbackInputNode::CreateLoopbackNodeExcludingProcessAsync(winrt::AudioGraph graph, uint32_t processId, winrt::AudioEncodingProperties encodingProperties)
    {
        return CreateLoopbackNodeAsync(graph, processId, encodingProperties, false);
    }
    void AudioLoopbackInputNode::StartCapture()
    {
        auto expected = false;
        if (m_started.compare_exchange_strong(expected, true))
        {
            winrt::check_hresult(MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xStartCapture, nullptr));
        }
    }
    void AudioLoopbackInputNode::Close()
    {
        auto expected = false;
        if (m_closed.compare_exchange_strong(expected, true))
        {
            if (m_queueID != 0)
            {
                MFUnlockWorkQueue(m_queueID);
            }

            winrt::check_hresult(MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xStopCapture, nullptr));

            // Wait for capture to stop
            m_captureStopped.wait();
        }
    }
    HRESULT AudioLoopbackInputNode::ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation)
    {
        HRESULT hr = S_OK;
        try
        {
            // Check for a successful activation result
            HRESULT hrActivateResult = E_UNEXPECTED;
            winrt::com_ptr<IUnknown> punkAudioInterface;
            winrt::check_hresult(operation->GetActivateResult(&hrActivateResult, punkAudioInterface.put()));
            winrt::check_hresult(hrActivateResult);

            // Get the pointer for the Audio Client
            m_audioClient = punkAudioInterface.as<IAudioClient>();

            auto encodingProperties = m_node.EncodingProperties();
            auto blockAlignment = encodingProperties.ChannelCount() * encodingProperties.BitsPerSample() / BITS_PER_BYTE;
            auto averageBytesPerSecond = encodingProperties.SampleRate() * blockAlignment;

            // The app can also call m_AudioClient->GetMixFormat instead to get the capture format.
            // 16 - bit PCM format.
            m_captureFormat.wFormatTag = WAVE_FORMAT_PCM;
            m_captureFormat.nChannels = static_cast<WORD>(encodingProperties.ChannelCount());
            m_captureFormat.nSamplesPerSec = static_cast<DWORD>(encodingProperties.SampleRate());
            m_captureFormat.wBitsPerSample = static_cast<WORD>(encodingProperties.BitsPerSample());
            m_captureFormat.nBlockAlign = static_cast<WORD>(blockAlignment);
            m_captureFormat.nAvgBytesPerSec = static_cast<DWORD>(averageBytesPerSecond);

            // Initialize the AudioClient in Shared Mode with the user specified buffer
            winrt::check_hresult(m_audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                200000,
                AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                &m_captureFormat,
                nullptr));

            // Get the capture client
            winrt::check_hresult(m_audioClient->GetService(IID_PPV_ARGS(&m_AudioCaptureClient)));

            // Create Async callback for sample events
            winrt::check_hresult(MFCreateAsyncResult(nullptr, &m_xSampleReady, nullptr, m_sampleReadyAsyncResult.put()));

            // Tell the system which event handle it should signal when an audio buffer is ready to be processed by the client
            winrt::check_hresult(m_audioClient->SetEventHandle(m_sampleReadyEvent.get()));
        }
        catch (...)
        {
            hr = winrt::to_hresult();
        }
        m_activateResult = hr;
        m_activateCompleted.SetEvent();
        return hr;
    }
    winrt::IAsyncOperation<api::AudioLoopbackInputNode> AudioLoopbackInputNode::CreateLoopbackNodeAsync(
        winrt::AudioGraph graph,
        uint32_t processId,
        winrt::AudioEncodingProperties encodingProperties,
        bool include)
    {
        auto session = winrt::make_self<AudioLoopbackInputNode>(graph, encodingProperties);
        auto completionHandler = session.as<IActivateAudioInterfaceCompletionHandler>();

        AUDIOCLIENT_ACTIVATION_PARAMS audioclientActivationParams = {};
        audioclientActivationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        audioclientActivationParams.ProcessLoopbackParams.ProcessLoopbackMode = include ?
            PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE : PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
        audioclientActivationParams.ProcessLoopbackParams.TargetProcessId = (DWORD)processId;

        PROPVARIANT activateParams = {};
        activateParams.vt = VT_BLOB;
        activateParams.blob.cbSize = sizeof(audioclientActivationParams);
        activateParams.blob.pBlobData = (BYTE*)&audioclientActivationParams;

        winrt::com_ptr<IActivateAudioInterfaceAsyncOperation> asyncOp;
        winrt::check_hresult(ActivateAudioInterfaceAsync(
            VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
            winrt::guid_of<IAudioClient>(),
            &activateParams,
            completionHandler.get(),
            asyncOp.put()));

        // Wait for activation completion
        co_await winrt::resume_on_signal(session->m_activateCompleted.get());

        winrt::check_hresult(session->m_activateResult);

        co_return session.as<api::AudioLoopbackInputNode>();
    }
    HRESULT AudioLoopbackInputNode::OnStartCapture(IMFAsyncResult* pResult)
    {
        UNREFERENCED_PARAMETER(pResult);
        try
        {
            // Start the capture
            winrt::check_hresult(m_audioClient->Start());

            MFPutWaitingWorkItem(m_sampleReadyEvent.get(), 0, m_sampleReadyAsyncResult.get(), &m_sampleReadyKey);
        }
        catch (...)
        {
            return winrt::to_hresult();
        }
        return S_OK;
    }
    HRESULT AudioLoopbackInputNode::OnStopCapture(IMFAsyncResult* pResult)
    {
        UNREFERENCED_PARAMETER(pResult);
        try
        {
            // Stop capture by cancelling Work Item
            // Cancel the queued work item (if any)
            {
                auto lock = m_sampleEventLock.lock();
                if (0 != m_sampleReadyKey)
                {
                    MFCancelWorkItem(m_sampleReadyKey);
                    m_sampleReadyKey = 0;
                }
            }

            m_audioClient->Stop(); // TODO: Ignore hr ???
            m_audioClient->Reset(); // TODO: Ignore hr ???
            m_sampleReadyAsyncResult = nullptr;

            winrt::check_hresult(MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_xFinishCapture, nullptr));
        }
        catch (...)
        {
            return winrt::to_hresult();
        }
        return S_OK;
    }
    HRESULT AudioLoopbackInputNode::OnFinishCapture(IMFAsyncResult* pResult)
    {
        UNREFERENCED_PARAMETER(pResult);
        try
        {
            m_captureStopped.SetEvent();
        }
        catch (...)
        {
            return winrt::to_hresult();
        }
        return S_OK;
    }
    HRESULT AudioLoopbackInputNode::OnSampleReady(IMFAsyncResult* pResult)
    {
        UNREFERENCED_PARAMETER(pResult);
        try
        {
            auto lock = m_sampleEventLock.lock();
            OnAudioSampleRequested();
            if (m_sampleReadyKey != 0)
            {
                // Re-queue work item for next sample
                winrt::check_hresult(MFPutWaitingWorkItem(m_sampleReadyEvent.get(), 0, m_sampleReadyAsyncResult.get(), &m_sampleReadyKey));
            }
        }
        catch (...)
        {
            return winrt::to_hresult();
        }
        return S_OK;
    }
    winrt::TimeSpan QPCToTimeSpan(uint64_t qpcTime)
    {
        LARGE_INTEGER frequency = {};
        QueryPerformanceFrequency(&frequency);
        using period = winrt::TimeSpan::period;
        static_assert(period::num == 1);
        const int64_t counter = static_cast<int64_t>(qpcTime);
        const int64_t whole = (counter / frequency.QuadPart) * period::den;
        const int64_t part = (counter % frequency.QuadPart) * period::den / frequency.QuadPart;
        winrt::TimeSpan timeSpan{ whole + part };
        return timeSpan;
    }
    void AudioLoopbackInputNode::OnAudioSampleRequested()
    {
        UINT32 FramesAvailable = 0;
        BYTE* Data = nullptr;
        DWORD dwCaptureFlags;
        UINT64 u64DevicePosition = 0;
        UINT64 u64QPCPosition = 0;
        DWORD cbBytesToCapture = 0;

        auto lock = m_critSec.lock();

        winrt::com_ptr<AudioLoopbackInputNode> self;
        self.copy_from(this);

        // A word on why we have a loop here;
        // Suppose it has been 10 milliseconds or so since the last time
        // this routine was invoked, and that we're capturing 48000 samples per second.
        //
        // The audio engine can be reasonably expected to have accumulated about that much
        // audio data - that is, about 480 samples.
        //
        // However, the audio engine is free to accumulate this in various ways:
        // a. as a single packet of 480 samples, OR
        // b. as a packet of 80 samples plus a packet of 400 samples, OR
        // c. as 48 packets of 10 samples each.
        //
        // In particular, there is no guarantee that this routine will be
        // run once for each packet.
        //
        // So every time this routine runs, we need to read ALL the packets
        // that are now available;
        //
        // We do this by calling IAudioCaptureClient::GetNextPacketSize
        // over and over again until it indicates there are no more packets remaining.
        while (SUCCEEDED(m_AudioCaptureClient->GetNextPacketSize(&FramesAvailable)) && FramesAvailable > 0)
        {
            cbBytesToCapture = FramesAvailable * m_captureFormat.nBlockAlign;

            // Get sample buffer
            winrt::check_hresult(m_AudioCaptureClient->GetBuffer(&Data, &FramesAvailable, &dwCaptureFlags, &u64DevicePosition, &u64QPCPosition));

            auto audioFrame = winrt::AudioFrame(cbBytesToCapture);
            {
                auto audioBuffer = audioFrame.LockBuffer(winrt::AudioBufferAccessMode::Write);
                auto audioBufferReference = audioBuffer.CreateReference();
                auto destByteAccess = audioBufferReference.as<::Windows::Foundation::IMemoryBufferByteAccess>();
                uint8_t* destBytes = nullptr;
                uint32_t destSize = 0;
                winrt::check_hresult(destByteAccess->GetBuffer(&destBytes, &destSize));

                memcpy_s(destBytes, destSize, Data, cbBytesToCapture);
                audioBuffer.Length(cbBytesToCapture);
            }
            auto milliseconds = FramesAvailable / (m_sampleRate / 1000);
            auto duration = std::chrono::milliseconds(milliseconds);
            audioFrame.Duration(std::optional<winrt::TimeSpan>(duration));
            audioFrame.RelativeTime(std::optional(QPCToTimeSpan(u64DevicePosition)));
            audioFrame.SystemRelativeTime(std::optional(QPCToTimeSpan(u64QPCPosition)));
            m_node.AddFrame(audioFrame);

            // Release buffer back
            m_AudioCaptureClient->ReleaseBuffer(FramesAvailable);
        }
    }
}
