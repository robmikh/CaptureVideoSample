#pragma once

class VideoEncoderDevice
{
public:
    static std::vector<std::shared_ptr<VideoEncoderDevice>> EnumerateAll();

    VideoEncoderDevice(winrt::com_ptr<IMFActivate> const& transformSource);

    std::wstring const& Name() { return m_name; }

    winrt::com_ptr<IMFTransform> CreateTransform();

private:
    winrt::com_ptr<IMFActivate> m_transformSource;
    std::wstring m_name;
};