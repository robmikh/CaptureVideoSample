#pragma once

class VideoEncoder
{
public:
    static std::vector<std::shared_ptr<VideoEncoder>> EnumerateAll();

    VideoEncoder(winrt::com_ptr<IMFTransform> const& transform);

    std::wstring const& Name() { return m_name; }

private:
    winrt::com_ptr<IMFTransform> m_transform;
    std::wstring m_name;
};