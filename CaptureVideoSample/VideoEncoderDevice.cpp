#include "pch.h"
#include "VideoEncoderDevice.h"

inline std::optional<std::wstring> GetStringAttribute(winrt::com_ptr<IMFAttributes> const& attributes, GUID const& attributeGuid)
{
    uint32_t resultLength = 0;
    HRESULT hr = attributes->GetStringLength(attributeGuid, &resultLength);
    if (SUCCEEDED(hr))
    {
        std::wstring result((size_t)resultLength + 1, L' ');
        winrt::check_hresult(attributes->GetString(attributeGuid, result.data(), (uint32_t)result.size(), &resultLength));
        result.resize(resultLength);
        return std::optional(std::move(result));
    }
    else if (hr != MF_E_ATTRIBUTENOTFOUND)
    {
        throw winrt::hresult_error(hr);
    }
    return std::nullopt;
}

inline auto EnumerateMFTs(GUID const& category, uint32_t const& flags, MFT_REGISTER_TYPE_INFO const* inputType, MFT_REGISTER_TYPE_INFO const* outputType)
{
    std::vector<winrt::com_ptr<IMFActivate>> transformSources;
    MFT_REGISTER_TYPE_INFO info = { MFMediaType_Video, MFVideoFormat_H264 };
    {
        wil::unique_cotaskmem_array_ptr<wil::com_ptr<IMFActivate>> activateArray;
        winrt::check_hresult(MFTEnumEx(
            category,
            flags,
            inputType,
            outputType,
            activateArray.put(),
            reinterpret_cast<uint32_t*>(activateArray.size_address())));

        if (activateArray.size() > 0)
        {
            for (auto&& activate : activateArray)
            {
                winrt::com_ptr<IMFActivate> transformSource;
                transformSource.copy_from(activate);
                transformSources.push_back(transformSource);
            }
        }
    }
    return transformSources;
}

std::vector<std::shared_ptr<VideoEncoderDevice>> VideoEncoderDevice::EnumerateAll()
{
    std::vector<std::shared_ptr<VideoEncoderDevice>> encoders;

    MFT_REGISTER_TYPE_INFO info = { MFMediaType_Video, MFVideoFormat_H264 };
    auto transformSources = EnumerateMFTs(
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
    if (auto name = GetStringAttribute(transformSource, MFT_FRIENDLY_NAME_Attribute))
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
