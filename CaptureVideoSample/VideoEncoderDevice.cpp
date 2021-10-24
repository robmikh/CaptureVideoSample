#include "pch.h"
#include "VideoEncoderDevice.h"

namespace util
{
    using namespace robmikh::common::uwp;
}

std::vector<std::shared_ptr<VideoEncoderDevice>> VideoEncoderDevice::EnumerateAll()
{
    std::vector<std::shared_ptr<VideoEncoderDevice>> encoders;

    MFT_REGISTER_TYPE_INFO info = { MFMediaType_Video, MFVideoFormat_H264 };
    auto transformSources = util::EnumerateMFTs(
        MFT_CATEGORY_VIDEO_ENCODER,
        MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_TRANSCODE_ONLY | MFT_ENUM_FLAG_SORTANDFILTER,
        nullptr,
        &info);

    for (auto transformSource : transformSources)
    {
        auto encoder = std::make_shared<VideoEncoderDevice>(transformSource);
        encoders.push_back(encoder);
    }

    return encoders;
}

VideoEncoderDevice::VideoEncoderDevice(winrt::com_ptr<IMFActivate> const& transformSource)
{
    std::wstring friendlyName;
    if (auto name = util::GetStringAttribute(transformSource, MFT_FRIENDLY_NAME_Attribute))
    {
        friendlyName = name.value();
    }
    else
    {
        friendlyName = L"Unknown";
    }

    m_transformSource = transformSource;
    m_name = friendlyName;
}

winrt::com_ptr<IMFTransform> VideoEncoderDevice::CreateTransform()
{
    winrt::com_ptr<IMFTransform> transform;
    winrt::check_hresult(m_transformSource->ActivateObject(winrt::guid_of<IMFTransform>(), transform.put_void()));
    return transform;
}
