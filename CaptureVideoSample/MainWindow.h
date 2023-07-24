#pragma once
#include <robmikh.common/DesktopWindow.h>

class App;

struct MainWindow : robmikh::common::desktop::DesktopWindow<MainWindow>
{
	static const std::wstring ClassName;
	MainWindow(std::wstring const& titleString, int width, int height, std::shared_ptr<App> app);
	LRESULT MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam);

private:
	enum class ApplicationState
	{
		Idle,
		Recording,
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

	static void RegisterWindowClass();
	void CreateControls(HINSTANCE instance);
	size_t GetIndexFromComboBox(HWND comboBox);
	winrt::fire_and_forget StartRecording();
	void OnRecordingStarted();
	void OnRecordingFinished();
	winrt::Windows::Graphics::SizeInt32 GetResolution(winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& source);
	uint32_t GetBitRate();
	uint32_t GetFrameRate();
	bool GetRecordMicrophone();
	bool GetRecordSystemAudio();
	void StopRecording();

private:
	std::shared_ptr<App> m_app;
	ApplicationState m_state = ApplicationState::Idle;
	HWND m_mainButton = nullptr;
	HWND m_resolutionComboBox = nullptr;
	HWND m_bitRateComboBox = nullptr;
	HWND m_fpsComboBox = nullptr;
	HWND m_topMostCheckBox = nullptr;
	HWND m_excludeCheckBox = nullptr;
	HWND m_microphoneCheckBox = nullptr;
	HWND m_systemAudioCheckBox = nullptr;
	std::vector<ResolutionEntry> m_resolutions;
	std::vector<BitRateEntry> m_bitRates;
	std::vector<FrameRateEntry> m_frameRates;
};