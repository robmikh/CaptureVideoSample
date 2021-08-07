#include "pch.h"
#include "VideoEncoder.h"

inline std::wstring GetStringAttribute(winrt::com_ptr<IMFAttributes> const& attributes, GUID const& attributeGuid)
{
    try
    {
        uint32_t resultLength = 0;
        winrt::check_hresult(attributes->GetStringLength(attributeGuid, &resultLength));
        std::wstring result(L' ', resultLength);
        winrt::check_hresult(attributes->GetString(attributeGuid, result.data(), result.size(), &resultLength));
        result.resize(resultLength);
        return result;
    }
    catch (winrt::hresult_error const error)
    {
        if (error.code() != MF_E_ATTRIBUTENOTFOUND)
        {
            throw error;
        }
    }
    return L"Unknown";
}

inline auto EnumerateMFTs(GUID const& category, uint32_t const& flags, MFT_REGISTER_TYPE_INFO const* inputType, MFT_REGISTER_TYPE_INFO const* outputType)
{
    std::vector<winrt::com_ptr<IMFTransform>> transforms;
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
                winrt::com_ptr<IMFTransform> transform;
                winrt::check_hresult(activate->ActivateObject(winrt::guid_of<IMFTransform>(), transform.put_void()));
                transforms.push_back(transform);
            }
        }
    }
    return transforms;
}

std::vector<std::shared_ptr<VideoEncoder>> VideoEncoder::EnumerateAll()
{
    std::vector<std::shared_ptr<VideoEncoder>> encoders;

    MFT_REGISTER_TYPE_INFO info = { MFMediaType_Video, MFVideoFormat_H264 };
    auto transforms = EnumerateMFTs(
        MFT_CATEGORY_VIDEO_ENCODER,
        MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
        nullptr,
        &info);

    for (auto transform : transforms)
    {
        auto encoder = std::make_shared<VideoEncoder>(transform);
        encoders.push_back(encoder);
    }

    return encoders;
}

VideoEncoder::VideoEncoder(winrt::com_ptr<IMFTransform> const& transform)
{
    winrt::com_ptr<IMFAttributes> attributes;
    winrt::check_hresult(transform->GetAttributes(attributes.put()));
    auto name = GetStringAttribute(attributes, MFT_FRIENDLY_NAME_Attribute);

    m_transform = transform;
    m_name = name;
}
