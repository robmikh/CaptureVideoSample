#pragma once
#include <robmikh.common/DesktopWindow.h>

class App;
class VideoEncoder;

struct MainWindow : robmikh::common::desktop::DesktopWindow<MainWindow>
{
	static const std::wstring ClassName;
	static void RegisterWindowClass();
	MainWindow(std::wstring const& titleString, int width, int height, std::shared_ptr<App> app);
	LRESULT MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam);

private:
	enum class ApplicationState
	{
		Idle,
		Recording,
	};

	struct EncoderEntry
	{
		std::wstring Display;
		std::shared_ptr<VideoEncoder> Encoder;
	};

	struct ResolutionEntry
	{
		std::wstring Display;
		winrt::Windows::Graphics::SizeInt32 Resolution;
	};

	struct BitRateEntry
	{
		std::wstring Display;
		uint32_t BitRate;
	};

	struct FrameRateEntry
	{
		std::wstring Display;
		uint32_t FrameRate;
	};

	void CreateControls(HINSTANCE instance);
	size_t GetIndexFromComboBox(HWND comboBox);
	winrt::fire_and_forget StartRecording();
	void OnRecordingStarted();
	void OnRecordingFinished();
	winrt::Windows::Graphics::SizeInt32 GetResolution(winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& source);
	uint32_t GetBitRate();
	uint32_t GetFrameRate();
	void StopRecording();

private:
	std::shared_ptr<App> m_app;
	ApplicationState m_state = ApplicationState::Idle;
	HWND m_mainButton = nullptr;
	HWND m_encoderComboBox = nullptr;
	HWND m_resolutionComboBox = nullptr;
	HWND m_bitRateComboBox = nullptr;
	HWND m_fpsComboBox = nullptr;
	HWND m_topMostCheckBox = nullptr;
	HWND m_excludeCheckBox = nullptr;
	std::vector<EncoderEntry> m_encoders;
	std::vector<ResolutionEntry> m_resolutions;
	std::vector<BitRateEntry> m_bitRates;
	std::vector<FrameRateEntry> m_frameRates;
};