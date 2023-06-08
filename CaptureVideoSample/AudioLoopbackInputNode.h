#pragma once
#include "AudioLoopbackInputNode.g.h"

namespace winrt::CaptureVideoSample::implementation
{
    struct AudioLoopbackInputNode : AudioLoopbackInputNodeT<AudioLoopbackInputNode, IActivateAudioInterfaceCompletionHandler>
    {
        AudioLoopbackInputNode(winrt::Windows::Media::Audio::AudioGraph const& graph, winrt::Windows::Media::MediaProperties::AudioEncodingProperties const& encodingProperties);
        ~AudioLoopbackInputNode() { Close(); }

        // AudioLoopbackInputNode
        static winrt::Windows::Foundation::IAsyncOperation<winrt::CaptureVideoSample::AudioLoopbackInputNode> CreateLoopbackNodeIncludingProcessAsync(winrt::Windows::Media::Audio::AudioGraph graph, uint32_t processId, winrt::Windows::Media::MediaProperties::AudioEncodingProperties encodingProperties);
        static winrt::Windows::Foundation::IAsyncOperation<winrt::CaptureVideoSample::AudioLoopbackInputNode> CreateLoopbackNodeExcludingProcessAsync(winrt::Windows::Media::Audio::AudioGraph graph, uint32_t processId, winrt::Windows::Media::MediaProperties::AudioEncodingProperties encodingProperties);
        winrt::Windows::Media::Audio::AudioFrameInputNode AudioGraphNode() { return m_node; }
        void StartCapture();
        void Close();

        // IActivateAudioInterfaceCompletionHandler
        STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation* operation);

        // Media async callbacks
        METHODASYNCCALLBACK(AudioLoopbackInputNode, StartCapture, OnStartCapture);
        METHODASYNCCALLBACK(AudioLoopbackInputNode, StopCapture, OnStopCapture);
        METHODASYNCCALLBACK(AudioLoopbackInputNode, SampleReady, OnSampleReady);
        METHODASYNCCALLBACK(AudioLoopbackInputNode, FinishCapture, OnFinishCapture);

    private:
        void CheckClosed()
        {
            if (m_closed.load())
            {
                throw winrt::hresult_error(RO_E_CLOSED);
            }
        }

        static winrt::Windows::Foundation::IAsyncOperation<winrt::CaptureVideoSample::AudioLoopbackInputNode> CreateLoopbackNodeAsync(winrt::Windows::Media::Audio::AudioGraph graph, uint32_t processId, winrt::Windows::Media::MediaProperties::AudioEncodingProperties encodingProperties, bool include);

        HRESULT OnStartCapture(IMFAsyncResult* pResult);
        HRESULT OnStopCapture(IMFAsyncResult* pResult);
        HRESULT OnFinishCapture(IMFAsyncResult* pResult);
        HRESULT OnSampleReady(IMFAsyncResult* pResult);

        void OnAudioSampleRequested();

    private:
        uint32_t m_sampleRate = 0;
        winrt::Windows::Media::Audio::AudioFrameInputNode m_node{ nullptr };

        winrt::com_ptr<IAudioClient> m_audioClient;
        winrt::com_ptr<IAudioCaptureClient> m_AudioCaptureClient;
        WAVEFORMATEX m_captureFormat = {};
        winrt::com_ptr<IMFAsyncResult> m_sampleReadyAsyncResult;

        wil::unique_event m_sampleReadyEvent;
        MFWORKITEM_KEY m_sampleReadyKey = 0;
        wil::critical_section m_critSec;
        DWORD m_queueID = 0;

        wil::unique_event m_activateCompleted;
        HRESULT m_activateResult = S_OK;

        wil::unique_event m_captureStopped;
        wil::critical_section m_sampleEventLock;

        std::atomic<bool> m_started = false;
        std::atomic<bool> m_closed = false;
    };
}
namespace winrt::CaptureVideoSample::factory_implementation
{
    struct AudioLoopbackInputNode : AudioLoopbackInputNodeT<AudioLoopbackInputNode, implementation::AudioLoopbackInputNode>
    {
    };
}
