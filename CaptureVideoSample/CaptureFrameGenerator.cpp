#include "pch.h"
#include "CaptureFrameGenerator.h"

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Graphics;
    using namespace Windows::Graphics::Capture;
    using namespace Windows::Graphics::DirectX;
    using namespace Windows::Graphics::DirectX::Direct3D11;
    using namespace Windows::Storage;
    using namespace Windows::UI::Composition;
}

CaptureFrameGenerator::CaptureFrameGenerator(
    winrt::IDirect3DDevice const& device,
    winrt::GraphicsCaptureItem const& item,
    winrt::SizeInt32 const& size)
{
    m_device = device;
    m_item = item;

    m_nextFrameEvent = wil::shared_event(wil::EventOptions::ManualReset);
    m_endEvent = wil::shared_event(wil::EventOptions::ManualReset);
    m_closedEvent = wil::shared_event(wil::EventOptions::ManualReset);

    m_framePool = winrt::Direct3D11CaptureFramePool::CreateFreeThreaded(
        m_device,
        winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        3,
        size);
    m_session = m_framePool.CreateCaptureSession(m_item);

    m_framePool.FrameArrived({ this, &CaptureFrameGenerator::OnFrameArrived });
    m_session.StartCapture();
}

CaptureFrameGenerator::~CaptureFrameGenerator()
{
    StopCapture();
    // We might end the capture before we ever get another frame.
    m_closedEvent.wait(200);
}

std::optional<winrt::Direct3D11CaptureFrame> CaptureFrameGenerator::TryGetNextFrame()
{
    {
        auto lock = m_lock.lock_exclusive();
        if (m_frames.empty() && m_endEvent.is_signaled())
        {
            return std::nullopt;
        }
        else if (!m_frames.empty())
        {
            std::optional result(m_frames.front());
            m_frames.pop_front();
            return result;
        }
    }

    m_nextFrameEvent.ResetEvent();
    std::vector<HANDLE> events = { m_endEvent.get(), m_nextFrameEvent.get() };
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
        std::optional result(m_frames.front());
        m_frames.pop_front();
        return result;
    }
}

void CaptureFrameGenerator::StopCapture()
{
    auto lock = m_lock.lock_exclusive();
    m_endEvent.SetEvent();
    m_framePool.Close();
    m_session.Close();
}

void CaptureFrameGenerator::OnFrameArrived(
    winrt::Direct3D11CaptureFramePool const& sender,
    winrt::IInspectable const&)
{
    auto lock = m_lock.lock_exclusive();
    if (m_endEvent.is_signaled())
    {
        m_closedEvent.SetEvent();
        return;
    }
    m_frames.push_back(sender.TryGetNextFrame());
    m_nextFrameEvent.SetEvent();
}
