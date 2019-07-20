// A basic test implementation of Netcore for IPC in Dolphin

#pragma warning(disable : 4564)


#include <string>

#include "VanguardClient.h"
#include "VanguardClientInitializer.h"
#include "Helpers.hpp"
#include "../libui_sdl/libui/ui.h"
#include "../libui_sdl/main.h"
#include "../Config.h"
#include "../libui_sdl/PlatformConfig.h"
#include "../NDS.h"
#include "../NDSCart.h"

#include <msclr/marshal_cppstd.h>
#using < system.dll>
#using < system.windows.forms.dll>
#using < system.collections.dll>

//If we provide just the dll name and then compile with /AI it works, but intellisense doesn't pick up on it, so we use a full relative path
#using <../../../../../RTCV/Build/NetCore.dll>
#using <../../../../../RTCV/Build/Vanguard.dll>
#using <../../../../../RTCV/Build/CorruptCore.dll>


using namespace cli;
using namespace System;
using namespace Text;
using namespace RTCV;
using namespace NetCore;
using namespace CorruptCore;
using namespace Vanguard;
using namespace Runtime::InteropServices;
using namespace Threading;
using namespace Collections::Generic;
using namespace Reflection;
using namespace Diagnostics;

#define SRAM_SIZE 25165824
#define ARAM_SIZE 16777216
#define EXRAM_SIZE 67108864

static int CPU_STEP_Count = 0;
static void EmuThreadExecute(Action ^ callback);
static void EmuThreadExecute(IntPtr ptr);

// Define this in here as it's managed and weird stuff happens if it's in a header
public
ref class VanguardClient
{
public:
	static NetCoreReceiver ^ receiver;
	static VanguardConnector ^ connector;

	static void OnMessageReceived(Object ^ sender, NetCoreEventArgs ^ e);
	static void SpecUpdated(Object ^ sender, SpecUpdateEventArgs ^ e);
	static void RegisterVanguardSpec();

	static void StartClient();
	static void RestartClient();
	static void StopClient();

	static void LoadRom(String ^ filename);
	static bool LoadState(std::string filename);
	static bool SaveState(String ^ filename, bool wait);

	static void LoadWindowPosition();
	static void SaveWindowPosition();
	static String^ GetSyncSettings();
	static void SetSyncSettings(String^ ss);

	static String ^ emuDir = IO::Path::GetDirectoryName(Assembly::GetExecutingAssembly()->Location);
	static String ^ logPath = IO::Path::Combine(emuDir, "EMU_LOG.txt");

	static System::Timers::Timer^ SettingsTimer;
	static System::Timers::Timer^ ReinitRendererTimer;
	static void SettingsSaveCallback(Object^ sender, System::Timers::ElapsedEventArgs^ e);
	static void ReinitRendererCallback(Object^ sender, System::Timers::ElapsedEventArgs^ e);

	static array<String ^> ^ configPaths;

	static volatile bool loading = false;
	static bool attached = false;
	static Object^ GenericLockObject = gcnew Object();
	static bool enableRTC = true;
};

static void EmuThreadExecute(Action ^ callback)
{
	EmuThreadExecute(Marshal::GetFunctionPointerForDelegate(callback));

}
static void EmuThreadExecute(IntPtr callbackPtr)
{
	int prevstatus = EmuRunning;
	EmuRunning = 2;
	while (EmuStatus != 2);
	static_cast<void(__stdcall*)(void)>(callbackPtr.ToPointer())();
	EmuRunning = prevstatus;
}

static PartialSpec ^
getDefaultPartial() {
	PartialSpec ^ partial = gcnew PartialSpec("VanguardSpec");
	partial->Set(VSPEC::NAME, "melonDS");
	partial->Set(VSPEC::SUPPORTS_RENDERING, false);
	partial->Set(VSPEC::SUPPORTS_CONFIG_MANAGEMENT, true);
	partial->Set(VSPEC::SUPPORTS_CONFIG_HANDOFF, true);
	partial->Set(VSPEC::SUPPORTS_KILLSWITCH, true);
	partial->Set(VSPEC::SUPPORTS_REALTIME, true);
	partial->Set(VSPEC::SUPPORTS_SAVESTATES, true);
	partial->Set(VSPEC::SUPPORTS_MIXED_STOCKPILE, true);
	partial->Set(VSPEC::CONFIG_PATHS, VanguardClient::configPaths);
	partial->Set(VSPEC::SYSTEM, String::Empty);
	partial->Set(VSPEC::GAMENAME, String::Empty);
	partial->Set(VSPEC::SYSTEMPREFIX, String::Empty);
	partial->Set(VSPEC::OPENROMFILENAME, String::Empty);
	partial->Set(VSPEC::OVERRIDE_DEFAULTMAXINTENSITY, 100000);
	partial->Set(VSPEC::SYNCSETTINGS, String::Empty);
	partial->Set(VSPEC::MEMORYDOMAINS_BLACKLISTEDDOMAINS, gcnew array<String ^>{
		"CartROM",
		"SharedWRAM",
		"ARM7WRAM"
	});
	partial->Set(VSPEC::SYSTEM, String::Empty);

	return partial;
}

void VanguardClient::SpecUpdated(Object ^ sender, SpecUpdateEventArgs ^ e)
{
	PartialSpec ^ partial = e->partialSpec;

	LocalNetCoreRouter::Route(NetcoreCommands::CORRUPTCORE,
		NetcoreCommands::REMOTE_PUSHVANGUARDSPECUPDATE, partial, true);
	LocalNetCoreRouter::Route(NetcoreCommands::UI, NetcoreCommands::REMOTE_PUSHVANGUARDSPECUPDATE,
		partial, true);
}

void VanguardClient::RegisterVanguardSpec()
{
	PartialSpec ^ emuSpecTemplate = gcnew PartialSpec("VanguardSpec");

	emuSpecTemplate->Insert(getDefaultPartial());

	AllSpec::VanguardSpec = gcnew FullSpec(emuSpecTemplate, true);
	// You have to feed a partial spec as a template

	if (VanguardClient::attached)
		RTCV::Vanguard::VanguardConnector::PushVanguardSpecRef(AllSpec::VanguardSpec);

	LocalNetCoreRouter::Route(NetcoreCommands::CORRUPTCORE, NetcoreCommands::REMOTE_PUSHVANGUARDSPEC,
		emuSpecTemplate, true);
	LocalNetCoreRouter::Route(NetcoreCommands::UI, NetcoreCommands::REMOTE_PUSHVANGUARDSPEC,
		emuSpecTemplate, true);
	AllSpec::VanguardSpec->SpecUpdated +=
		gcnew EventHandler<SpecUpdateEventArgs ^>(&VanguardClient::SpecUpdated);
}

// Lifted from Bizhawk
static Assembly^ CurrentDomain_AssemblyResolve(Object ^ sender, ResolveEventArgs ^ args) {
	try
	{
		Trace::WriteLine("Entering AssemblyResolve\n" + args->Name + "\n" +
			args->RequestingAssembly);
		String ^ requested = args->Name;
		Monitor::Enter(AppDomain::CurrentDomain);
		{
			array<Assembly ^> ^ asms = AppDomain::CurrentDomain->GetAssemblies();
			for (int i = 0; i < asms->Length; i++)
			{
				Assembly ^ a = asms[i];
				if (a->FullName == requested)
				{
					return a;
				}
			}

			AssemblyName ^ n = gcnew AssemblyName(requested);
			// load missing assemblies by trying to find them in the dll directory
			String ^ dllname = n->Name + ".dll";
			String ^ directory = IO::Path::Combine(
				IO::Path::GetDirectoryName(Assembly::GetExecutingAssembly()->Location), "..", "RTCV");
			String ^ fname = IO::Path::Combine(directory, dllname);
			if (!IO::File::Exists(fname))
			{
				Trace::WriteLine(fname + " doesn't exist");
				return nullptr;
			}

			// it is important that we use LoadFile here and not load from a byte array; otherwise
			// mixed (managed/unamanged) assemblies can't load
			Trace::WriteLine("Loading " + fname);
			return Assembly::LoadFile(fname);
		}
	}
	catch (Exception ^ e)
	{
		Trace::WriteLine("Something went really wrong in AssemblyResolve. Send this to the devs\n" +
			e);
		return nullptr;
	}
	finally
	{
	  Monitor::Exit(AppDomain::CurrentDomain);
	}
}

void VanguardClient::SettingsSaveCallback(Object^ sender, System::Timers::ElapsedEventArgs^ e)
{
	VanguardClient::SaveWindowPosition();
}
void VanguardClient::ReinitRendererCallback(Object^ sender, System::Timers::ElapsedEventArgs^ e)
{
	ApplyNewSettings(3); //Force reinit of renderer
}
// Create our VanguardClient
void VanguardClientInitializer::StartVanguardClient()
{
	System::Windows::Forms::Form ^ dummy = gcnew System::Windows::Forms::Form();
	IntPtr Handle = dummy->Handle;
	SyncObjectSingleton::SyncObject = dummy;
	SyncObjectSingleton::EmuInvokeDelegate = gcnew SyncObjectSingleton::ActionDelegate(&EmuThreadExecute);

	//Some callbacks (such as window position changed) just don't work. Because of this, use a timer to save those settings every 5 minutes.
	VanguardClient::SettingsTimer = gcnew System::Timers::Timer(300000);
	VanguardClient::SettingsTimer->Elapsed += gcnew System::Timers::ElapsedEventHandler(&VanguardClient::SettingsSaveCallback);
	VanguardClient::ReinitRendererTimer = gcnew System::Timers::Timer(150);
	VanguardClient::ReinitRendererTimer->Elapsed += gcnew System::Timers::ElapsedEventHandler(&VanguardClient::ReinitRendererCallback);
	VanguardClient::ReinitRendererTimer->AutoReset = false;

	// Start everything
	VanguardClient::configPaths = gcnew array<String ^>{
		System::IO::Path::Combine(VanguardClient::emuDir, "melonDS.ini")
	};

	VanguardClient::StartClient();
	VanguardClient::RegisterVanguardSpec();
	RtcCore::StartEmuSide();

	// Lie if we're in attached
	if (VanguardClient::attached)
		VanguardConnector::ImplyClientConnected();

	VanguardClient::LoadWindowPosition();

}

// Create our VanguardClient
void VanguardClientInitializer::Initialize()
{
	// This has to be in its own method where no other dlls are used so the JIT can compile it
	AppDomain::CurrentDomain->AssemblyResolve +=
		gcnew ResolveEventHandler(CurrentDomain_AssemblyResolve);

	StartVanguardClient();
}

void VanguardClient::StartClient()
{
	NetCore_Extensions::ConsoleHelper::CreateConsole(logPath);
	NetCore_Extensions::ConsoleHelper::HideConsole();
	// Can't use contains
	auto args = Environment::GetCommandLineArgs();
	for (int i = 0; i < args->Length; i++)
	{
		if (args[i] == "-CONSOLE")
		{
			NetCore_Extensions::ConsoleHelper::ShowConsole();
		}
		if (args[i] == "-ATTACHED")
		{
			attached = true;
		}
		if (args[i] == "-DISABLERTC")
		{
			enableRTC = false;
		}
	}

	receiver = gcnew NetCoreReceiver();
	receiver->Attached = attached;
	receiver->MessageReceived += gcnew EventHandler<NetCoreEventArgs ^>(&VanguardClient::OnMessageReceived);
	connector = gcnew VanguardConnector(receiver);
	VanguardClient::SettingsTimer->Enabled = true;
}


void VanguardClient::RestartClient()
{
	VanguardClient::StopClient();
	VanguardClient::StartClient();
}

void VanguardClient::StopClient()
{
	connector->Kill();
	connector = nullptr;
	VanguardClient::SettingsTimer->Enabled = false;
}
void VanguardClient::LoadWindowPosition()
{
	if (connector == nullptr)
		return;

	int winX, winY;
	String^ s = RTCV::NetCore::Params::ReadParam("MELON_LOCATION");
	if (s == nullptr)
		return;

	array<String^>^ loc = s->Split(',');
	winX = Int32::Parse(loc[0]);
	winY = Int32::Parse(loc[1]);
	uiWindowSetBorderless(MainWindow, true);
	uiWindowSetPosition(MainWindow, winX, winY);
	uiWindowSetBorderless(MainWindow, false);
}
void VanguardClient::SaveWindowPosition()
{
	int winX = 0, winY = 0;
	uiWindowPosition(MainWindow, &winX, &winY);
	RTCV::NetCore::Params::SetParam("MELON_LOCATION", winX + "," + winY);
}
String^ VanguardClient::GetSyncSettings()
{
	Dictionary<String^, String^>^ ssDico = gcnew Dictionary<String^, String^>();
	ssDico["ScreenRotation"] = Convert::ToString(ScreenRotation);
	return JsonHelper::Serialize(ssDico);
}
void VanguardClient::SetSyncSettings(String^ ss)
{
	auto ssDico = JsonHelper::Deserialize<Dictionary<String^,String^>^>(ss);
	System::String ^ out = "v";
	if (ssDico->TryGetValue("ScreenRotation", out))
	{
		auto a = Int32::Parse(out);
		OnSetScreenRotation(nullptr, MainWindow, (void*)&a);
	}
	Trace::WriteLine(out);
}

#pragma region MemoryDomains

//For some reason if we do these in another class, melon won't build
public
ref class MainRAM : RTCV::CorruptCore::IMemoryDomain
{
public:
	property System::String^ Name { virtual System::String^ get(); }
	property long long Size { virtual long long get(); }
	property int WordSize { virtual int get(); }
	property bool BigEndian { virtual bool get(); }
	virtual unsigned char PeekByte(long long addr);
	virtual array<unsigned char> ^ PeekBytes(long long address, int length);
	virtual void PokeByte(long long addr, unsigned char val);
};

ref class VRAM : RTCV::CorruptCore::IMemoryDomain
{
public:
	property System::String^ Name { virtual System::String^ get(); }
	property long long Size { virtual long long get(); }
	property int WordSize { virtual int get(); }
	property bool BigEndian { virtual bool get(); }
	virtual unsigned char PeekByte(long long addr);
	virtual array<unsigned char> ^ PeekBytes(long long address, int length);
	virtual void PokeByte(long long addr, unsigned char val);
};
ref class CartROM : RTCV::CorruptCore::IMemoryDomain
{
public:
	property System::String^ Name { virtual System::String^ get(); }
	property long long Size { virtual long long get(); }
	property int WordSize { virtual int get(); }
	property bool BigEndian { virtual bool get(); }
	virtual unsigned char PeekByte(long long addr);
	virtual array<unsigned char> ^ PeekBytes(long long address, int length);
	virtual void PokeByte(long long addr, unsigned char val);
};
ref class SharedWRAM : RTCV::CorruptCore::IMemoryDomain
{
public:
	property System::String^ Name { virtual System::String^ get(); }
	property long long Size { virtual long long get(); }
	property int WordSize { virtual int get(); }
	property bool BigEndian { virtual bool get(); }
	virtual unsigned char PeekByte(long long addr);
	virtual array<unsigned char> ^ PeekBytes(long long address, int length);
	virtual void PokeByte(long long addr, unsigned char val);
};

ref class ARM7WRAM : RTCV::CorruptCore::IMemoryDomain
{
public:
	property System::String^ Name { virtual System::String^ get(); }
	property long long Size { virtual long long get(); }
	property int WordSize { virtual int get(); }
	property bool BigEndian { virtual bool get(); }
	virtual unsigned char PeekByte(long long addr);
	virtual array<unsigned char> ^ PeekBytes(long long address, int length);
	virtual void PokeByte(long long addr, unsigned char val);
};

#define WORD_SIZE 4
#define BIG_ENDIAN false

#define MAIN_RAM_OFFSET 0x02000000
#define VRAM_OFFSET 0x06000000
#define VRAM_SIZE 0x00800000

#define SHAREDWRAM_SIZE 0x8000
#define ARM7WRAM_SIZE 0x10000

#define VRAM_ABG_OFFSET 0x00000000
#define VRAM_BBG_OFFSET 0x00200000
#define VRAM_AOBJ_OFFSET 0x00400000
#define VRAM_BOBJ_OFFSET 0x00600000

delegate void MessageDelegate(Object^);
#pragma region MainRam
String^ MainRAM::Name::get()
{
	return "MainRAM";
}

long long MainRAM::Size::get()
{
	return MAIN_RAM_SIZE;
}

int MainRAM::WordSize::get()
{
	return WORD_SIZE;
}

bool MainRAM::BigEndian::get()
{
	return BIG_ENDIAN;
}

unsigned char MainRAM::PeekByte(long long addr)
{
	if (addr < MAIN_RAM_SIZE)
	{
		// Convert the address
		addr = addr + MAIN_RAM_OFFSET;

		return NDS::ARM9Read8(static_cast<u32>(addr));
	}
	return 0;
}

void MainRAM::PokeByte(long long addr, unsigned char val)
{
	if (addr < MAIN_RAM_SIZE)
	{
		// Convert the address
		addr = addr + MAIN_RAM_OFFSET;
		NDS::ARM9Write8(static_cast<u32>(addr), val);
	}
}

array<unsigned char>^ MainRAM::PeekBytes(long long address, int length)
{
	array<unsigned char> ^ bytes = gcnew array<unsigned char>(length);
	for (int i = 0; i < length; i++)
	{
		bytes[i] = PeekByte(address + i);
	}
	return bytes;
}
#pragma endregion
	
#pragma region VRAM
	String^ VRAM::Name::get()
	{
		return "VRAM";
	}

	long long VRAM::Size::get()
	{
		return VRAM_SIZE;
	}

	int VRAM::WordSize::get()
	{
		return WORD_SIZE;
	}

	bool VRAM::BigEndian::get()
	{
		return BIG_ENDIAN;
	}

	unsigned char VRAM::PeekByte(long long addr)
	{
		if (addr < VRAM_SIZE)
		{
			// Convert the address
			addr = addr + VRAM_OFFSET;

			return NDS::ARM9Read8(static_cast<u32>(addr));
		}
		return 0;
	}

	void VRAM::PokeByte(long long addr, unsigned char val)
	{
		if (addr < VRAM_SIZE)
		{
			// Convert the address
			addr = addr + VRAM_OFFSET;
			NDS::ARM9Write8(static_cast<u32>(addr), val);
		}
	}

	array<unsigned char>^ VRAM::PeekBytes(long long address, int length)
	{
		array<unsigned char> ^ bytes = gcnew array<unsigned char>(length);
		for (int i = 0; i < length; i++)
		{
			bytes[i] = PeekByte(address + i);
		}
		return bytes;
	}
#pragma endregion

#pragma region CARTROM
	String^ CartROM::Name::get()
	{
		return "CartROM";
	}

	long long CartROM::Size::get()
	{
		return NDSCart::CartROMSize;
	}

	int CartROM::WordSize::get()
	{
		return WORD_SIZE;
	}

	bool CartROM::BigEndian::get()
	{
		return BIG_ENDIAN;
	}

	unsigned char CartROM::PeekByte(long long addr)
	{
		if (!NDSCart::CartInserted) return 0;

		if (addr < NDSCart::CartROMSize)
		{
			return NDSCart::CartROM[addr];
		}
		return 0;
	}

	void CartROM::PokeByte(long long addr, unsigned char val)
	{
		if (!NDSCart::CartInserted) return;
		if (addr < NDSCart::CartROMSize)
		{
			NDSCart::CartROM[addr] = val;
		}
	}

	array<unsigned char>^ CartROM::PeekBytes(long long address, int length)
	{
		array<unsigned char> ^ bytes = gcnew array<unsigned char>(length);
		for (int i = 0; i < length; i++)
		{
			bytes[i] = PeekByte(address + i);
		}
		return bytes;
	}
#pragma endregion

#pragma region SharedWRAM
	String^ SharedWRAM::Name::get()
	{
		return "SharedWRAM";
	}

	long long SharedWRAM::Size::get()
	{
		return SHAREDWRAM_SIZE;
	}

	int SharedWRAM::WordSize::get()
	{
		return WORD_SIZE;
	}

	bool SharedWRAM::BigEndian::get()
	{
		return BIG_ENDIAN;
	}

	unsigned char SharedWRAM::PeekByte(long long addr)
	{
		if (addr < SHAREDWRAM_SIZE)
		{
			return NDS::SharedWRAM[addr];
		}
		return 0;
	}

	void SharedWRAM::PokeByte(long long addr, unsigned char val)
	{
		if (addr < SHAREDWRAM_SIZE)
		{
			NDS::SharedWRAM[addr] = val;
		}
	}

	array<unsigned char>^ SharedWRAM::PeekBytes(long long address, int length)
	{
		array<unsigned char> ^ bytes = gcnew array<unsigned char>(length);
		for (int i = 0; i < length; i++)
		{
			bytes[i] = PeekByte(address + i);
		}
		return bytes;
	}
#pragma endregion

#pragma region Arm7WRAM
	String^ ARM7WRAM::Name::get()
	{
		return "ARM7WRAM";
	}

	long long ARM7WRAM::Size::get()
	{
		return ARM7WRAM_SIZE;
	}

	int ARM7WRAM::WordSize::get()
	{
		return WORD_SIZE;
	}

	bool ARM7WRAM::BigEndian::get()
	{
		return BIG_ENDIAN;
	}

	unsigned char ARM7WRAM::PeekByte(long long addr)
	{
		if (addr < ARM7WRAM_SIZE)
		{
			return NDS::ARM7WRAM[addr];
		}
		return 0;
	}

	void ARM7WRAM::PokeByte(long long addr, unsigned char val)
	{
		if (addr < ARM7WRAM_SIZE)
		{
			NDS::ARM7WRAM[addr] = val;
		}
	}

	array<unsigned char>^ ARM7WRAM::PeekBytes(long long address, int length)
	{
		array<unsigned char> ^ bytes = gcnew array<unsigned char>(length);
		for (int i = 0; i < length; i++)
		{
			bytes[i] = PeekByte(address + i);
		}
		return bytes;
	}
#pragma endregion

static array<MemoryDomainProxy ^> ^
GetInterfaces() {
	array<MemoryDomainProxy ^> ^ interfaces = gcnew array<MemoryDomainProxy ^>(5);
	interfaces[0] = (gcnew MemoryDomainProxy(gcnew MainRAM));
	interfaces[1] = (gcnew MemoryDomainProxy(gcnew SharedWRAM));
	interfaces[2] = (gcnew MemoryDomainProxy(gcnew ARM7WRAM));
	interfaces[3] = (gcnew MemoryDomainProxy(gcnew VRAM));
	interfaces[4] = (gcnew MemoryDomainProxy(gcnew CartROM));
	return interfaces;
}

static bool RefreshDomains(bool updateSpecs = true)
{
	array<MemoryDomainProxy ^> ^ oldInterfaces =
		AllSpec::VanguardSpec->Get<array<MemoryDomainProxy ^> ^>(VSPEC::MEMORYDOMAINS_INTERFACES);
	array<MemoryDomainProxy ^> ^ newInterfaces = GetInterfaces();

	// Bruteforce it since domains can change inconsistently in some configs and we keep code
	// consistent between implementations
	bool domainsChanged = false;
	if (oldInterfaces == nullptr)
		domainsChanged = true;
	else
	{
		domainsChanged = oldInterfaces->Length != newInterfaces->Length;
		for (int i = 0; i < oldInterfaces->Length; i++)
		{
			if (domainsChanged)
				break;
			if (oldInterfaces[i]->Name != newInterfaces[i]->Name)
				domainsChanged = true;
			if (oldInterfaces[i]->Size != newInterfaces[i]->Size)
				domainsChanged = true;
		}
	}

	if (updateSpecs)
	{
		AllSpec::VanguardSpec->Update(VSPEC::MEMORYDOMAINS_INTERFACES, newInterfaces, true, true);
		LocalNetCoreRouter::Route(NetcoreCommands::CORRUPTCORE,
			NetcoreCommands::REMOTE_EVENT_DOMAINSUPDATED, domainsChanged, true);
	}

	return domainsChanged;
}

#pragma endregion

static void STEP_CORRUPT()  // errors trapped by CPU_STEP
{
	if (!VanguardClient::enableRTC)
		return;
	StepActions::Execute();
	CPU_STEP_Count++;
	bool autoCorrupt = RtcCore::AutoCorrupt;
	long errorDelay = RtcCore::ErrorDelay;
	if (autoCorrupt && CPU_STEP_Count >= errorDelay)
	{
		CPU_STEP_Count = 0;
		array<String ^> ^ domains = AllSpec::UISpec->Get<array<String ^> ^>("SELECTEDDOMAINS");

		BlastLayer ^ bl = RtcCore::GenerateBlastLayer(domains, -1);
		if (bl != nullptr)
			bl->Apply(false, true);
	}
}

#pragma region Hooks
void VanguardClientUnmanaged::CORE_STEP()
{
	if (!VanguardClient::enableRTC)
		return;
	// Any step hook for corruption
	STEP_CORRUPT();
}

// This is on the main thread not the emu thread
void VanguardClientUnmanaged::LOAD_GAME_START(std::string romPath)
{
	if (!VanguardClient::enableRTC)
		return;
	StepActions::ClearStepBlastUnits();
	CPU_STEP_Count = 0;

	String ^ gameName = Helpers::utf8StringToSystemString(romPath);
	AllSpec::VanguardSpec->Update(VSPEC::OPENROMFILENAME, gameName, true, true);
}

void VanguardClientUnmanaged::LOAD_GAME_DONE()
{
	if (!VanguardClient::enableRTC)
		return;
	PartialSpec ^ gameDone = gcnew PartialSpec("VanguardSpec");

	try
	{
		gameDone->Set(VSPEC::SYSTEM, "melonDS");
		gameDone->Set(VSPEC::SYSTEMPREFIX, "melonDS");
		gameDone->Set(VSPEC::SYSTEMCORE, "DS");
		gameDone->Set(VSPEC::CORE_DISKBASED, false);

		String ^ oldGame = AllSpec::VanguardSpec->Get<String ^>(VSPEC::GAMENAME);

		String ^ gameName = VanguardClientUnmanaged::GAME_NAME.ToString();

		char replaceChar = L'-';
		gameDone->Set(VSPEC::GAMENAME, CorruptCore_Extensions::MakeSafeFilename(gameName, replaceChar));
		
		AllSpec::VanguardSpec->Update(gameDone, true, false);

		bool domainsChanged = RefreshDomains(true);

		if (oldGame != gameName)
		{
			LocalNetCoreRouter::Route(NetcoreCommands::UI,
				NetcoreCommands::RESET_GAME_PROTECTION_IF_RUNNING, true);
		}
	}
	catch (Exception ^ e)
	{
		Trace::WriteLine(e->ToString());
	}
	VanguardClient::loading = false;
}
void VanguardClientUnmanaged::GAME_CLOSED()
{
	if (!VanguardClient::enableRTC)
		return;
	AllSpec::VanguardSpec->Update(VSPEC::OPENROMFILENAME, "", true, true);
}

int VanguardClientUnmanaged::GAME_NAME = 1;

bool VanguardClientUnmanaged::RTC_OSD_ENABLED()
{
	if (!VanguardClient::enableRTC)
		return true;
	if (RTCV::NetCore::Params::IsParamSet(RTCSPEC::CORE_EMULATOROSDDISABLED))
		return false;
	return true;
}

#pragma endregion

/*ENUMS FOR THE SWITCH STATEMENT*/
enum COMMANDS
{
	SAVESAVESTATE,
	LOADSAVESTATE,
	REMOTE_LOADROM,
	REMOTE_CLOSEGAME,
	REMOTE_DOMAIN_GETDOMAINS,
	REMOTE_KEY_SETSYNCSETTINGS,
	REMOTE_KEY_SETSYSTEMCORE,
	REMOTE_EVENT_EMU_MAINFORM_CLOSE,
	REMOTE_EVENT_EMUSTARTED,
	REMOTE_ISNORMALADVANCE,
	REMOTE_EVENT_CLOSEEMULATOR,
	REMOTE_ALLSPECSSENT,
	REMOTE_POSTCORRUPTACTION,
	UNKNOWN
};

inline COMMANDS CheckCommand(String ^ inString)
{
	if (inString == "LOADSAVESTATE")
		return LOADSAVESTATE;
	if (inString == "SAVESAVESTATE")
		return SAVESAVESTATE;
	if (inString == "REMOTE_LOADROM")
		return REMOTE_LOADROM;
	if (inString == "REMOTE_CLOSEGAME")
		return REMOTE_CLOSEGAME;
	if (inString == "REMOTE_ALLSPECSSENT")
		return REMOTE_ALLSPECSSENT;
	if (inString == "REMOTE_DOMAIN_GETDOMAINS")
		return REMOTE_DOMAIN_GETDOMAINS;
	if (inString == "REMOTE_KEY_SETSYSTEMCORE")
		return REMOTE_KEY_SETSYSTEMCORE;
	if (inString == "REMOTE_KEY_SETSYNCSETTINGS")
		return REMOTE_KEY_SETSYNCSETTINGS;
	if (inString == "REMOTE_EVENT_EMU_MAINFORM_CLOSE")
		return REMOTE_EVENT_EMU_MAINFORM_CLOSE;
	if (inString == "REMOTE_EVENT_EMUSTARTED")
		return REMOTE_EVENT_EMUSTARTED;
	if (inString == "REMOTE_ISNORMALADVANCE")
		return REMOTE_ISNORMALADVANCE;
	if (inString == "REMOTE_EVENT_CLOSEEMULATOR")
		return REMOTE_EVENT_CLOSEEMULATOR;
	if (inString == "REMOTE_ALLSPECSSENT")
		return REMOTE_ALLSPECSSENT;
	if (inString == "REMOTE_POSTCORRUPTACTION")
		return REMOTE_POSTCORRUPTACTION;
	return UNKNOWN;
}

/* IMPLEMENT YOUR COMMANDS HERE */
void VanguardClient::LoadRom(String ^ filename)
{
	std::string path = Helpers::systemStringToUtf8String(filename);
	loading = true;

	int prevstatus = EmuRunning;
	EmuRunning = 2;
	while (EmuStatus != 2);
	TryLoadROM((char*)path.c_str(), prevstatus);

	// We have to do it this way to prevent deadlock due to synced calls. It sucks but it's required
	// at the moment
	while (loading)
	{
		Thread::Sleep(20);
		System::Windows::Forms::Application::DoEvents();
	}

	Thread::Sleep(10);  // Give the emu thread a chance to recover
	return;
}

bool VanguardClient::LoadState(std::string filename)
{
	StepActions::ClearStepBlastUnits();
	Main::LoadState(filename.c_str());
	return true;
}

bool VanguardClient::SaveState(String ^ filename, bool wait)
{
	if (true)
	{
		std::string s = Helpers::systemStringToUtf8String(filename);
		const char* converted_filename = s.c_str();
		Main::SaveState(converted_filename);
		return true;
	}
	return false;
}


// No fun anonymous classes with closure here
#pragma region Delegates
void StopGame()
{
	Stop(false);
}

void Quit()
{
	System::Environment::Exit(0);
}

void AllSpecsSent()
{
	AllSpec::VanguardSpec->Update(VSPEC::EMUDIR, VanguardClient::emuDir, true, true);
	VanguardClient::LoadWindowPosition();
}
#pragma endregion

/* THIS IS WHERE YOU HANDLE ANY RECEIVED MESSAGES */
void VanguardClient::OnMessageReceived(Object ^ sender, NetCoreEventArgs ^ e)
{
	NetCoreMessage ^ message = e->message;
	NetCoreAdvancedMessage ^ advancedMessage;

	if (Helpers::is<NetCoreAdvancedMessage ^>(message))
		advancedMessage = static_cast<NetCoreAdvancedMessage ^>(message);

	switch (CheckCommand(message->Type))
	{
	case REMOTE_ALLSPECSSENT:
	{
		auto g = gcnew SyncObjectSingleton::GenericDelegate(&AllSpecsSent);
		SyncObjectSingleton::FormExecute(g);
	}
	break;

	case LOADSAVESTATE:
	{
		array<Object ^> ^ cmd = static_cast<array<Object ^> ^>(advancedMessage->objectValue);
		String ^ path = static_cast<String ^>(cmd[0]);
		std::string converted_path = Helpers::systemStringToUtf8String(path);


		// Load up the sync settings
		String ^ settingStr = AllSpec::VanguardSpec->Get<String ^>(VSPEC::SYNCSETTINGS);
		if (!String::IsNullOrEmpty(settingStr))
		{
			VanguardClient::SetSyncSettings(settingStr);
		}
		bool success = LoadState(converted_path);
		e->setReturnValue(success);
	}
	break;

	case SAVESAVESTATE:
	{
		String ^ Key = (String ^)(advancedMessage->objectValue);

		//Save the syncsettings
		AllSpec::VanguardSpec->Set(VSPEC::SYNCSETTINGS, VanguardClient::GetSyncSettings());

		// Build the shortname
		String ^ quickSlotName = Key + ".timejump";
		// Get the prefix for the state

		String ^ gameName = VanguardClientUnmanaged::GAME_NAME.ToString();

		char replaceChar = L'-';
		String ^ prefix = CorruptCore_Extensions::MakeSafeFilename(gameName, replaceChar);
		prefix = prefix->Substring(prefix->LastIndexOf('\\') + 1);

		String ^ path = nullptr;
		// Build up our path
		path = RtcCore::workingDir + IO::Path::DirectorySeparatorChar + "SESSION" + IO::Path::DirectorySeparatorChar + prefix + "." + quickSlotName + ".State";

		// If the path doesn't exist, make it
		IO::FileInfo ^ file = gcnew IO::FileInfo(path);
		if (file->Directory != nullptr && file->Directory->Exists == false)
			file->Directory->Create();
		VanguardClient::SaveState(path, true);
		e->setReturnValue(path);
	}
	break;

	case REMOTE_LOADROM:
	{
		String ^ filename = (String ^)advancedMessage->objectValue;
		//Dolphin DEMANDS the rom is loaded from the main thread
		System::Action<String^>^a = gcnew Action<String^>(&LoadRom);
		SyncObjectSingleton::FormExecute<String ^>(a, filename);
	}
	break;

	case REMOTE_CLOSEGAME:
	{
		SyncObjectSingleton::GenericDelegate ^ g =
			gcnew SyncObjectSingleton::GenericDelegate(&StopGame);
		SyncObjectSingleton::FormExecute(g);
	}
	break;

	case REMOTE_DOMAIN_GETDOMAINS:
	{
		RefreshDomains();
	}
	break;

	case REMOTE_KEY_SETSYNCSETTINGS:
	{
		String ^ settings = (String ^)(advancedMessage->objectValue);
		AllSpec::VanguardSpec->Set(VSPEC::SYNCSETTINGS, settings);
	}
	break;

	case REMOTE_KEY_SETSYSTEMCORE:
	{
		// Do nothing
	}
	break;

	case REMOTE_EVENT_EMUSTARTED:
	{
		// Do nothing
	}
	break;

	case REMOTE_ISNORMALADVANCE:
	{
		// Todo - Dig out fast forward?
		e->setReturnValue(true);
	}
	break;
	case REMOTE_POSTCORRUPTACTION:
	{
		if(Config::ScreenUseGL || (Config::_3DRenderer != 0))
			VanguardClient::ReinitRendererTimer->Enabled = true;
	}
	break;

	case REMOTE_EVENT_EMU_MAINFORM_CLOSE:
	case REMOTE_EVENT_CLOSEEMULATOR:
	{
		//Don't allow re-entry on this
		Monitor::Enter(VanguardClient::GenericLockObject);
		{
			VanguardClient::SaveWindowPosition();
			uiQuit();
			Quit();
		}
		Monitor::Exit(VanguardClient::GenericLockObject);
	}
	break;

	default:
		break;
	}
}

