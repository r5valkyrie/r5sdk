#pragma once
#include "miles_types.h"
#include "miles/include/mss.h"

#define CSOM_MAX_ASYNC_FILE_HANDLES 128
#define CSOM_MAX_ASYNC_FILE_HANDLES_MASK (CSOM_MAX_ASYNC_FILE_HANDLES-1)
#define CSOM_MAX_FILE_NAME 64
#define CSOM_MAX_LOADED_BANKS 16

struct CSOM_AsyncFile_s
{
	u64 asyncRequestId;
	char fileName[CSOM_MAX_FILE_NAME];
	int fileHandle;
	size_t readOffset;
	size_t readStart;
	size_t fileSize;
};

struct CSOM_BankList_s
{
	char banks[CSOM_MAX_LOADED_BANKS][CSOM_MAX_FILE_NAME];
	int bankCount;
};

/* ==== WASAPI THREAD SERVICE =========================================================================================================================================== */
inline void*(*v_MilesAllocEx)(const size_t size, const u8 flags, void* const driver, const char* const allocSrcFileName, const int allocSrcFileLine);
inline void(*v_MilesQueueEventRun)(Miles::Queue*, const char*);
inline void(*v_MilesBankPatch)(Miles::Bank*, char*, char*);
inline unsigned int (*v_MilesSampleSetSourceRaw)(__int64 a1, __int64 a2, unsigned int a3, int a4, unsigned __int16 a5, bool a6);
inline unsigned int (*v_MilesEventGetDetails)(__int64 a1, __int64 a2, __int64 a3, __int64 a4, __int64 a5, __int64 a6, void* const releaseList);

// Typedef for the 6-param version used internally (Miles 10.0.42 - game version)
typedef unsigned int (*MilesEventGetDetails_t)(__int64 bankHandle, __int64* templateIdPtr, const char** nameOut, int* param3, int* param4, int* param5);
inline __int64 (*v_MilesSampleCreate)(__int64 a1, __int64 a2, unsigned __int8 a3);
inline __int64 (*v_MilesSamplePlay)(void* sample);
inline __int64 (*v_MilesSamplePause)(_BYTE* sample);
inline __int64 (*v_MilesSamplePauseFade)(__int64 sample);
inline __int64 (*v_MilesSampleDestroy)(__int64 sample);
inline __int64 (*v_MilesSampleGetDurationMs)(__int64 sample);
inline __int64 (*v_MilesSampleGetDurationSamples)(__int64 sample);
inline __int64 (*v_MilesSampleSetListenerMask)(__int64 a1, unsigned int a2);
inline __int64 (*v_MilesSampleSetVolumeLevel)(void* sample, float volume);
inline __int64 (*v_MilesSampleGetRouteCount)(__int64 a1);
inline __int64 (*v_MilesSampleCreateRoute)(__int64 a1, __int64 a2, __int64 a3, unsigned __int8 a4);
inline __int64 (*v_MilesSampleGetRoute)(__int64 a1, unsigned int a2);

inline bool(*v_CSOM_Initialize)();
inline void(*v_CSOM_InitializeBankList)(CSOM_BankList_s* const bankList);
inline void(*v_CSOM_LogFunc)(int64_t nLogLevel, const char* pszMessage);
inline void(*v_CSOM_RunFrame)(char a1, char a2, float a3, float a4);

inline s32(*v_CSOM_MilesAsync_FileRead)(MilesAsyncRead* const request);
inline s32(*v_CSOM_MilesAsync_FileStatus)(MilesAsyncRead* const request, const u32 i_MS);
inline s32(*v_CSOM_MilesAsync_FileCancel)(MilesAsyncRead* const request);

inline void(*v_CSOM_AddEventToQueue)(const char* eventName);

inline void(*v_ProcessClientAnimEvent)(__int64 a1, __int64 a2, __int64 a3, unsigned int a4, const char* a5, __int64 a6, __int64 a7);

// Core audio event dispatcher - action 1=play, 2=stop, 3=?
inline __int64(*v_Miles_DispatchEvent)(unsigned __int16 action);

// Stop signal handler - sends "signal" to running events 
inline __int64(*v_Miles_StopEventByHash)(__int64 eventHash);

// Stop all sounds with filter
inline __int64(*v_Miles_StopAllSounds)(__int64 a1, __int64 a2, __int64 filterType);

// Event hash lookup from name (FNV-1a)
inline void(*v_Miles_SetEventHashFromName)(char* eventName);

// Event hash lookup from hash value directly
inline void(*v_Miles_SetEventHashFromValue)(__int64 eventHash);

// Miles event table entry processor (sub_1409AF740) - gets event details from template ID
inline char(*v_Miles_ProcessEventEntry)(__int64 entryPtr);

// Audio listener bitmask processing function (sub_1409190F0)
// This function processes 3D audio spatialization and listener masks.
// In Miles 10.0.62, internal data structures changed causing a crash when
// clearing listener bitmask bits. We patch to NOP out the problematic instruction.
inline void(*v_Miles_ProcessListenerMasks)();

// Global pointers for audio state
inline uint64_t* g_Miles_QueuedEventData;    // qword_1655B9640 - event data structure
inline uint64_t* g_Miles_EventTimeOffset;     // qword_1655B9648 - time offset
inline uint64_t* g_Miles_QueuedEventHash;     // qword_1655B9658 - current event hash
inline uint32_t* g_Miles_EntityId;            // qword_1655B9660 - entity ID
inline float* g_Miles_SoundPosition;          // qword_1655B9668 - position XYZ

// Miles event table globals (for enumerating all events)
inline uint64_t* g_Miles_EventTableBase;      // qword_14D4E4AE8 - event table base pointer
inline uint32_t* g_Miles_HashBucketTable;     // dword_14D4E0AE8 - hash bucket index table (4096 entries)
inline uint64_t* g_Miles_EventCount;          // qword_14D4DF258 - number of events in table
inline uint64_t* g_Miles_BankHandle;          // qword_14D4DF238 - Miles bank handle for MilesEventGetDetails

struct CSOM_GlobalState_s
{
	char gap0[24];
	bool mismatchedBuildTag;
	char gap19[63];
	uintptr_t queuedEventHash;
	char gap60[4];
	Vector3D queuedSoundPosition;
	char gap70[24];
	float soundMasterVolume;
	char gap8c[28];
	void* samplesXlogType;
	char gapB0[8];
	void* dumpXlogType;
	char gapC0[48];
	void* driver;
	void* queue;
	char gap100[40];
	CSOM_BankList_s bankList;
	char gap52c[4];
	void* loadedBanks[CSOM_MAX_LOADED_BANKS];
	char gap5b0[273016];
	CSOM_AsyncFile_s asyncFiles[CSOM_MAX_ASYNC_FILE_HANDLES];
	size_t asyncRequestIdGen;
	char gap46430[4110];
	HANDLE milesInitializedEvent;
	HANDLE milesThread;
	char gap47450[272];
	char milesOutputBuffer[1024];
	char unk[96];
};

struct EventTableEntry {
	// A 64-bit value that serves as an index to the next entry in case of a hash collision.
	// A value of 0 indicates the end of the list for this bucket.
	uint64_t nextEntryIndex; // Offset 0x00

	// The full 64-bit FNV-1a hash of the normalized event name.
	uint64_t fullHash;       // Offset 0x08

	// The remaining 32 bytes would contain the actual event data, like a function pointer, etc.
	char eventData[32];      // Offset 0x10
};

inline CSOM_GlobalState_s* g_milesGlobals;

// Handle level changes (call from Mod_HandleLevelChanged)
void Miles_HandleLevelChanged();

void FmodDebugEventPrint(const char* eventType, const char* eventName);

///////////////////////////////////////////////////////////////////////////////
class MilesCore : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("MilesAllocEx", v_MilesAllocEx);
		LogFunAdr("MilesQueueEventRun", v_MilesQueueEventRun);
		LogFunAdr("MilesBankPatch", v_MilesBankPatch);
		LogFunAdr("MilesSampleSetSourceRaw", v_MilesSampleSetSourceRaw);
		LogFunAdr("MilesEventGetDetails", v_MilesEventGetDetails);
		LogFunAdr("CSOM_Initialize", v_CSOM_Initialize);
		LogFunAdr("CSOM_InitializeBankList", v_CSOM_InitializeBankList);
		LogFunAdr("CSOM_LogFunc", v_CSOM_LogFunc);
		LogFunAdr("CSOM_RunFrame", v_CSOM_RunFrame);
		LogFunAdr("CSOM_MilesAsync_FileRead", v_CSOM_MilesAsync_FileRead);
		LogFunAdr("CSOM_MilesAsync_FileStatus", v_CSOM_MilesAsync_FileStatus);
		LogFunAdr("CSOM_MilesAsync_FileCancel", v_CSOM_MilesAsync_FileCancel);
		LogFunAdr("CSOM_AddEventToQueue", v_CSOM_AddEventToQueue);
		LogFunAdr("Miles_DispatchEvent", v_Miles_DispatchEvent);
		LogFunAdr("Miles_StopEventByHash", v_Miles_StopEventByHash);
		LogFunAdr("Miles_StopAllSounds", v_Miles_StopAllSounds);
		LogFunAdr("Miles_SetEventHashFromName", v_Miles_SetEventHashFromName);
		LogFunAdr("Miles_SetEventHashFromValue", v_Miles_SetEventHashFromValue);
		LogFunAdr("Miles_ProcessEventEntry", v_Miles_ProcessEventEntry);
		LogFunAdr("Miles_ProcessListenerMasks", v_Miles_ProcessListenerMasks);
		LogVarAdr("g_Miles_QueuedEventData", g_Miles_QueuedEventData);
		LogVarAdr("g_Miles_QueuedEventHash", g_Miles_QueuedEventHash);
		LogVarAdr("g_Miles_EventTableBase", g_Miles_EventTableBase);
		LogVarAdr("g_Miles_HashBucketTable", g_Miles_HashBucketTable);
		LogVarAdr("g_Miles_BankHandle", g_Miles_BankHandle);
		LogVarAdr("g_milesGlobals", g_milesGlobals);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "40 53 56 57 41 54 41 55 41 56 41 57 48 81 EC ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ??").GetPtr(v_CSOM_Initialize);
		Module_FindPattern(g_GameDll, "40 55 41 54 41 55 48 8D 6C 24 ?? 48 81 EC ?? ?? ?? ?? 83 65").GetPtr(v_CSOM_InitializeBankList);
		Module_FindPattern(g_GameDll, "40 53 48 83 EC 20 48 8B DA 48 8D 15 ?? ?? ?? ??").GetPtr(v_CSOM_LogFunc);
		Module_FindPattern(g_GameDll, "48 8B C4 F3 0F 11 50 ?? 88 50").GetPtr(v_CSOM_RunFrame);
		Module_FindPattern(g_GameDll, "4C 8B DC 53 55 56 57 48 81 EC").GetPtr(v_CSOM_MilesAsync_FileRead);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 57 48 83 EC ?? 8B B9 ?? ?? ?? ?? 48 8B D9 83 FF").GetPtr(v_CSOM_MilesAsync_FileStatus);
		Module_FindPattern(g_GameDll, "40 53 48 83 EC ?? 8B 81 ?? ?? ?? ?? 48 8B D9 83 F8 ?? 75 ?? B8").GetPtr(v_CSOM_MilesAsync_FileCancel);
		Module_FindPattern(g_GameDll, "0F B6 11 4C 8B C1").GetPtr(v_CSOM_AddEventToQueue);

		Module_FindPattern(g_GameDll, "48 89 5C 24 18 57 41 56 41 57 48 83 EC 40 48 8B F9 41 8B D9 8B 89 D8 16").GetPtr(v_ProcessClientAnimEvent);

		// Core Miles event dispatcher (sub_140956660) - catches all play/stop events
		Module_FindPattern(g_GameDll, "48 83 EC 28 48 8B 05 ?? ?? ?? ?? 48 83 F8 01 75").GetPtr(v_Miles_DispatchEvent);

		// Stop event by hash signal (sub_14095B860) - sends signal to stop event
		Module_FindPattern(g_GameDll, "48 8B C1 4C 8D 15 ?? ?? ?? ?? 25 FF 07 00 00 4C").GetPtr(v_Miles_StopEventByHash);

		// Stop all sounds (sub_14095B5C0) 
		Module_FindPattern(g_GameDll, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 54 41 56 41 57 48 83 EC 20 48 8D").GetPtr(v_Miles_StopAllSounds);

		// Set event hash from name - FNV-1a hash (sub_14095B480)
		Module_FindPattern(g_GameDll, "0F B6 11 4C 8B C1 84 D2 75 0C 48 C7 05").GetPtr(v_Miles_SetEventHashFromName);

		// Set event hash from value directly (sub_14095B420)
		Module_FindPattern(g_GameDll, "4C 8B 05 ?? ?? ?? ?? 48 8B C1 25 FF 0F 00 00 48 8B D1").GetPtr(v_Miles_SetEventHashFromValue);

		// Miles event entry processor (sub_1409AF740) - processes event table entries
		Module_FindPattern(g_GameDll, "40 55 53 56 41 56 48 8D 6C 24 ?? 48 81 EC 38 01").GetPtr(v_Miles_ProcessEventEntry);

		// Audio listener bitmask processing (sub_1409190F0) - needs patching for Miles 10.0.62
		// Pattern: Function prologue with unique stack frame size 0x50B10
		Module_FindPattern(g_GameDll, "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 F3 0F 11 5C 24 20 55 41 56 41 57 B8 10 0B 05 00").GetPtr(v_Miles_ProcessListenerMasks);

		g_RadAudioSystemDll.GetExportedSymbol("MilesAllocEx").GetPtr(v_MilesAllocEx);
		g_RadAudioSystemDll.GetExportedSymbol("MilesQueueEventRun").GetPtr(v_MilesQueueEventRun);
		g_RadAudioSystemDll.GetExportedSymbol("MilesBankPatch").GetPtr(v_MilesBankPatch);
		g_RadAudioSystemDll.GetExportedSymbol("MilesSampleSetSourceRaw").GetPtr(v_MilesSampleSetSourceRaw);
		g_RadAudioSystemDll.GetExportedSymbol("MilesEventGetDetails").GetPtr(v_MilesEventGetDetails);
		g_RadAudioSystemDll.GetExportedSymbol("MilesSampleCreate").GetPtr(v_MilesSampleCreate);
		g_RadAudioSystemDll.GetExportedSymbol("MilesSamplePause").GetPtr(v_MilesSamplePause);
		g_RadAudioSystemDll.GetExportedSymbol("MilesSamplePauseFade").GetPtr(v_MilesSamplePauseFade);
		g_RadAudioSystemDll.GetExportedSymbol("MilesSampleDestroy").GetPtr(v_MilesSampleDestroy);
		g_RadAudioSystemDll.GetExportedSymbol("MilesSampleGetDurationMs").GetPtr(v_MilesSampleGetDurationMs);
		g_RadAudioSystemDll.GetExportedSymbol("MilesSampleGetDurationSamples").GetPtr(v_MilesSampleGetDurationSamples);
		g_RadAudioSystemDll.GetExportedSymbol("MilesSamplePlay").GetPtr(v_MilesSamplePlay);
        g_RadAudioSystemDll.GetExportedSymbol("MilesSampleSetListenerMask").GetPtr(v_MilesSampleSetListenerMask);
        g_RadAudioSystemDll.GetExportedSymbol("MilesSampleSetVolumeLevel").GetPtr(v_MilesSampleSetVolumeLevel);
		
		//Miles Route
		g_RadAudioSystemDll.GetExportedSymbol("MilesSampleGetRouteCount").GetPtr(v_MilesSampleGetRouteCount);
        g_RadAudioSystemDll.GetExportedSymbol("MilesSampleCreateRoute").GetPtr(v_MilesSampleCreateRoute);
        g_RadAudioSystemDll.GetExportedSymbol("MilesSampleGetRoute").GetPtr(v_MilesSampleGetRoute);

	}
	virtual void GetVar(void) const
	{
		g_milesGlobals = CMemory(v_CSOM_Initialize).FindPatternSelf("48 8D", CMemory::Direction::DOWN, 0x50).ResolveRelativeAddressSelf(0x3, 0x7).RCast<CSOM_GlobalState_s*>();

		// Resolve audio state globals from the dispatcher function
		// Layout: qword_1655B9640 (cmd), 9648 (time), 9650 (?), 9658 (hash), 9660 (entId), 9668 (pos)
		if (v_Miles_DispatchEvent)
		{
			CMemory dispatchMem(v_Miles_DispatchEvent);
			// qword_1655B9658 is at offset 0x4 from function start
			g_Miles_QueuedEventHash = dispatchMem.Offset(0x4).ResolveRelativeAddressSelf(0x3, 0x7).RCast<uint64_t*>();
			// Other globals are contiguous in memory
			g_Miles_QueuedEventData = reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(g_Miles_QueuedEventHash) - 0x18);  // 9658 - 0x18 = 9640
			g_Miles_SoundPosition = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(g_Miles_QueuedEventHash) + 0x10);  // 9658 + 0x10 = 9668
		}

		// Resolve event table globals from v_Miles_SetEventHashFromValue (sub_14095B420)
		// This function loads: mov r8, cs:qword_14D4E4AE8 (at +0) and lea rcx, dword_14D4E0AE8 (at +0x12)
		if (v_Miles_SetEventHashFromValue)
		{
			CMemory hashFromValueMem(v_Miles_SetEventHashFromValue);
			// qword_14D4E4AE8 at offset 0x0: mov r8, [rip+disp32] = 4C 8B 05 xx xx xx xx
			// ResolveRelativeAddressSelf(dispOffset, instrEnd) - disp at +3, instruction ends at +7
			// Note: Make a copy since ResolveRelativeAddressSelf modifies in-place
			CMemory eventTableMem = hashFromValueMem;
			g_Miles_EventTableBase = eventTableMem.ResolveRelativeAddressSelf(0x3, 0x7).RCast<uint64_t*>();
			// dword_14D4E0AE8 at offset 0x12: lea rcx, [rip+disp32] = 48 8D 0D xx xx xx xx
			// From instruction start at +0x12, disp at +0x15, instruction ends at +0x19
			CMemory hashBucketMem = hashFromValueMem.Offset(0x12);
			g_Miles_HashBucketTable = hashBucketMem.ResolveRelativeAddressSelf(0x3, 0x7).RCast<uint32_t*>();
		}

		// Resolve bank handle from v_Miles_ProcessEventEntry (sub_1409AF740)
		// Bank handle is loaded at offset 0x31: mov rcx, [rip+disp32] = 48 8B 0D xx xx xx xx
		if (v_Miles_ProcessEventEntry)
		{
			CMemory eventEntryMem(v_Miles_ProcessEventEntry);
			// From instruction start at +0x31, disp at +0x34, instruction ends at +0x38
			CMemory bankHandleMem = eventEntryMem.Offset(0x31);
			g_Miles_BankHandle = bankHandleMem.ResolveRelativeAddressSelf(0x3, 0x7).RCast<uint64_t*>();
		}
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////