#pragma once
//RTC_Hijack - Move a bunch of stuff to a header so we can access it
class Main
{
	public:
		static void SaveState(const char* filename);
		static void LoadState(const char* filename);
	
};

extern int EmuRunning;
extern int ScreenRotation;
extern volatile int EmuStatus;
extern void TryLoadROM(char* file, int prevstatus);
extern void Stop(bool internal);
extern void CloseAllDialogs();
extern uiWindow* MainWindow;
extern void OnSetScreenRotation(uiMenuItem* item, uiWindow* window, void* param);
extern void ApplyNewSettings(int type);