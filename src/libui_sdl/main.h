#pragma once

class Main
{
	public:
		static void SaveState(const char* filename);
		static void LoadState(const char* filename);
	
};

extern int EmuRunning;
extern volatile int EmuStatus;
extern void TryLoadROM(char* file, int prevstatus);
extern void Stop(bool internal);
extern void CloseAllDialogs();
