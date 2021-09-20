#pragma once

struct MediaBufferGuard
{
    struct BufferInfo
    {
        byte* Bits;
        DWORD MaxLength;
        DWORD CurrentLength;
    };

    MediaBufferGuard(winrt::com_ptr<IMFMediaBuffer> const& buffer)
    {
        m_buffer = buffer;
        winrt::check_hresult(buffer->Lock(&m_info.Bits, &m_info.MaxLength, &m_info.CurrentLength));
    }

    ~MediaBufferGuard()
    {
        winrt::check_hresult(m_buffer->Unlock());
    }

    BufferInfo const& Info() { return m_info; }

private:
    winrt::com_ptr<IMFMediaBuffer> m_buffer;
    BufferInfo m_info = {};
};