//===============================================================================//
//
// Purpose: Client Sound Miles implementation
//
//===============================================================================//
#include "core/stdafx.h"
#include "tier0/fasttimer.h"
#include "tier0/commandline.h"
#include "tier1/cvar.h"
#include <cmath>
#include <algorithm>
#include "rtech/core/strutils.h"
#include "rtech/async/asyncio.h"
#include "rtech/pak/pakstate.h"
#include "filesystem/filesystem.h"
#include "pluginsystem/modsystem.h"
#include "ebisusdk/EbisuSDK.h"
#include "miles_impl.h"
#include "game/client/viewrender.h"
#include "miles/src/sdk/shared/rrthreads2.h"
#include "../fmod/audio_backend.h"
#include "public/game/client/icliententity.h"
#include <unordered_map>
#include <mutex>
#include <codecs/fmod/studio_backend.cpp>

//-----------------------------------------------------------------------------
// Console variables
//-----------------------------------------------------------------------------
static ConVar miles_debug("miles_debug", "0", FCVAR_DEVELOPMENTONLY, "Enables debug prints for the Miles Sound System", "1 = print; 0 (zero) = no print");
static ConVar miles_warnings("miles_warnings", "0", FCVAR_DEVELOPMENTONLY, "Enables warning prints for the Miles Sound System", "1 = print; 0 (zero) = no print");
static ConVar fmod_debug("fmod_debug", "0", FCVAR_DEVELOPMENTONLY, "Enables debug prints for the FMOD Sound System", "1 = print; 0 (zero) = no print");
ConVar* fmod_debug = &s_fmod_debug;  // Pointer for studio_backend.cpp

// Level change detection
static ConVar miles_wav_auto_stop_on_level_change("miles_wav_auto_stop_on_level_change", "1", FCVAR_RELEASE, "Automatically stop custom audio when level changes (1 = enabled, 0 = disabled)");

static ICustomAudioBackend* be = nullptr;
static bool g_milesEventsCached = false;  // Flag for deferred Miles event enumeration

// Forward declarations for hash caching (used in CSOM_Initialize)
static uint64_t ComputeMilesEventHash(const char* eventName);
void CacheEventNameByHash(const char* eventName, uint64_t hash);
const char* LookupEventNameByRawHash(uint64_t rawHash);
static void CacheMilesEventNames();

/*void SetEventPositionOverride(const char* eventName, const Vector3D& position)
{
	std::lock_guard<std::mutex> lg(s_eventPositionMutex);
	s_eventPositionOverrides[eventName] = position;
	if (wav_debug.GetBool())
		Msg(eDLL_T::AUDIO, "Set position override for event '%s': (%.1f, %.1f, %.1f)\n", 
			eventName, position.x, position.y, position.z);
}*/

/*bool GetEventPositionOverride(const char* eventName, Vector3D& position)
{
	std::lock_guard<std::mutex> lg(s_eventPositionMutex);
	auto it = s_eventPositionOverrides.find(eventName);
	if (it != s_eventPositionOverrides.end())
	{
		position = it->second;
		// Remove the override after using it (single-use)
		s_eventPositionOverrides.erase(it);
		return true;
	}
	return false;
}*/


//-----------------------------------------------------------------------------
// Purpose: Called when a level change is detected - stops all custom audio
//-----------------------------------------------------------------------------
void Miles_HandleLevelChanged()
{
	/*if (miles_wav_auto_stop_on_level_change.GetBool())
	{
		if (wav_debug.GetBool())
		{
			Msg(eDLL_T::AUDIO, "Level change detected via Mod_HandleLevelChanged, stopping all custom audio\n");
		}
		StopAllCustomAudio();
	}*/
}

//-----------------------------------------------------------------------------
// Purpose: initializes the miles sound system
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
static bool CSOM_Initialize()
{
	const char* pszLanguage = HEbisuSDK_GetLanguage();
	const bool isDefaultLanguage = V_stricmp(pszLanguage, MILES_DEFAULT_LANGUAGE) == 0;

	if (!isDefaultLanguage)
	{
		if ((V_stricmp(pszLanguage, "schinese") == 0) || (V_stricmp(pszLanguage, "tchinese") == 0))
			pszLanguage = "mandarin"; // schinese and tchinese use the mandarin bank.

		const bool useShipSound = !CommandLine()->FindParm("-devsound") || CommandLine()->FindParm("-shipsound");
		char baseStreamFilePath[MAX_OSPATH];

		V_snprintf(baseStreamFilePath, sizeof(baseStreamFilePath), "%s\\general_%s.mstr", useShipSound ? "audio\\ship" : "audio\\dev", pszLanguage);
		bool found = FileExists(baseStreamFilePath);

		if (!found && ModSystem()->IsEnabled())
		{
			ModSystem()->LockModList();

			// Check for it in our mods.
			FOR_EACH_VEC(ModSystem()->GetModList(), i)
			{
				const CModSystem::ModInstance_t* const mod = ModSystem()->GetModList()[i];

				if (!mod->IsEnabled())
					continue;

				const CUtlString modLookupPath = mod->GetBasePath() + baseStreamFilePath;
				const char* const pModLookupPath = modLookupPath.String();

				found = FileExists(pModLookupPath);

				if (found)
					break;
			}

			ModSystem()->UnlockModList();
		}

		if (!found)
		{
			// if the requested language for miles does not have a MSTR file present,
			// throw a non-fatal error and force MILES_DEFAULT_LANGUAGE as a fallback if
			// we are loading MILES_DEFAULT_LANGUAGE and the file is still not found, we
			// can let it hit the regular engine error, since that is not recoverable.
			Error(eDLL_T::AUDIO, NO_ERROR, "%s: attempted to load language '%s' but the required streaming source file (%s) was not found, falling back to '%s'...\n",
				__FUNCTION__, pszLanguage, baseStreamFilePath, MILES_DEFAULT_LANGUAGE);

			pszLanguage = MILES_DEFAULT_LANGUAGE;
		}

		miles_language->SetValue(pszLanguage);
	}

	Msg(eDLL_T::AUDIO, "%s: initializing MSS with language: '%s'\n", __FUNCTION__, pszLanguage);
	CFastTimer initTimer;

	initTimer.Start();
	const bool bResult = v_CSOM_Initialize();
	initTimer.End();

	Msg(eDLL_T::AUDIO, "%s: %s (%f seconds)\n", __FUNCTION__, bResult ? "success" : "failure", initTimer.GetDuration().GetSeconds());

	{
		static ICustomAudioBackend* s_backend = nullptr;
		if (!s_backend)
		{
			s_backend = CreateFMODStudioBackend();
			if (s_backend && s_backend->Initialize())
			{
				SetActiveCustomAudioBackend(s_backend);
				be = s_backend;
				Msg(eDLL_T::AUDIO, "FMOD: Initialized FMOD Studio backend for custom audio\n");

				// Pre-cache all FMOD event names with their Miles FNV-1a hashes
				// This allows us to match events even when Miles uses pre-computed hashes
				int eventsCached = 0;
				s_backend->EnumerateEvents([](const char* eventPath, void* userData) {
					int* count = static_cast<int*>(userData);
					// Strip "event:/" prefix for Miles hash computation
					const char* eventName = eventPath;
					if (V_strnicmp(eventName, "event:/", 7) == 0)
						eventName = eventPath + 7;

					uint64_t hash = ComputeMilesEventHash(eventName);
					CacheEventNameByHash(eventName, hash);
					(*count)++;
				}, &eventsCached);
			}
			else
			{
				if (fmod_debug->GetBool())
					Msg(eDLL_T::AUDIO, "Failed to initialize FMOD Studio backend; falling back to Miles for custom audio\n");
				delete s_backend;
				s_backend = nullptr;
			}
		}
	}

	return bResult;
}

//-----------------------------------------------------------------------------
// Purpose: appends banks from list to be loaded
//-----------------------------------------------------------------------------
static void CSOM_AppendBanksFromList(CSOM_BankList_s* const bankList, const char* const filePath, const bool mandatory)
{
	const int errorCode = mandatory ? EXIT_FAILURE : 0;
	RSON::Node_t* root = nullptr;

#define ERROR_AND_RETURN(fmt, ...) \
		do {\
			Error(eDLL_T::AUDIO, errorCode, "Error loading Miles Bank list from '%s': "##fmt, filePath, ##__VA_ARGS__); \
			if (root) {\
				RSON_Free(root, AlignedMemAlloc()); \
				AlignedMemAlloc()->Free(root); \
			}\
			return; \
		} while(0)\

	if (bankList->bankCount == CSOM_MAX_LOADED_BANKS)
	{
		ERROR_AND_RETURN("Out of room -- already reached code limit of %d.\n", CSOM_MAX_LOADED_BANKS);
		return;
	}

	CUtlBuffer buf;

	if (!FileSystem()->ReadFile(filePath, nullptr, buf))
	{
		if (mandatory) // Only exit if the main file doesn't exist.
			ERROR_AND_RETURN("Could not load file.\n");

		return;
	}

	const RSON::eFieldType rootType = (RSON::eFieldType)(RSON::eFieldType::RSON_ARRAY | RSON::eFieldType::RSON_VALUE);
	root = RSON::LoadFromBuffer(filePath, (char*)buf.Base(), rootType);

	const RSON::eFieldType expectType = (RSON::eFieldType)(RSON::eFieldType::RSON_ARRAY | RSON::eFieldType::RSON_OBJECT);

	if (!root || root->type != expectType)
		ERROR_AND_RETURN("Data should be an array of objects.\n");

	const int numSlotsLeft = (CSOM_MAX_LOADED_BANKS - bankList->bankCount);

	if (root->valueCount > numSlotsLeft)
		ERROR_AND_RETURN("Too many banks -- code limit is %d.\n", CSOM_MAX_LOADED_BANKS);

	bool nameSetForBank = false;

	for (int i = 0; i < root->valueCount; i++)
	{
		const RSON::Field_t* const key = root->GetArrayValue(i)->GetSubKey();

		if (!key)
			continue;

		if (V_strcmp(key->name, "name") != 0)
			ERROR_AND_RETURN("Only valid key is 'name', not '%s'.\n", key->name);

		if (nameSetForBank)
			ERROR_AND_RETURN("Each bank must have exactly one name.\n");

		nameSetForBank = true;

		if (key->node.type != RSON::eFieldType::RSON_STRING)
			ERROR_AND_RETURN("'name' must be a single string.\n");

		const char* const bankToAdd = key->GetString();

		// Make sure this bank wasn't already added.
		for (int j = 0; j < bankList->bankCount; j++)
		{
			if (V_stricmp(bankList->banks[j], bankToAdd) == 0)
				ERROR_AND_RETURN("Each bank must be unique; '%s' was already listed.\n", bankToAdd);
		}

		V_strncpy(bankList->banks[bankList->bankCount++], bankToAdd, CSOM_MAX_FILE_NAME);
	}

	RSON_Free(root, AlignedMemAlloc());
	AlignedMemAlloc()->Free(root);

#undef ERROR_AND_RETURN
}

#define CSOM_BANK_LIST_FILE "scripts/audio/banks.rson"

//-----------------------------------------------------------------------------
// Purpose: initializes the bank list object dictating which banks to load
//-----------------------------------------------------------------------------
static void CSOM_InitializeBankList(CSOM_BankList_s* const bankList)
{
	bankList->bankCount = 0;
	CSOM_AppendBanksFromList(bankList, CSOM_BANK_LIST_FILE, true);

	if (ModSystem()->IsEnabled())
	{
		ModSystem()->LockModList();

		// Add banks from our mods.
		FOR_EACH_VEC(ModSystem()->GetModList(), i)
		{
			const CModSystem::ModInstance_t* const mod = ModSystem()->GetModList()[i];

			if (!mod->IsEnabled())
				continue;

			const CUtlString lookupPath = mod->GetBasePath() + CSOM_BANK_LIST_FILE;
			CSOM_AppendBanksFromList(bankList, lookupPath.String(), false);
		}

		ModSystem()->UnlockModList();
	}
}

//-----------------------------------------------------------------------------
// Purpose: logs debug output emitted from the Miles Sound System
// Input  : nLogLevel - 
//          pszMessage - 
//-----------------------------------------------------------------------------
static void CSOM_LogFunc(int64_t nLogLevel, const char* pszMessage)
{
	Msg(eDLL_T::AUDIO, "%s\n", pszMessage);
	v_CSOM_LogFunc(nLogLevel, pszMessage);
}

// Capture flag to sniff the formatted Buffer string produced by sub_1405F2F60
static thread_local bool g_captureBufferFrom_sub_1405F2F60 = false;

//-----------------------------------------------------------------------------
// Purpose: runs the event queue
//-----------------------------------------------------------------------------
void MilesQueueEventRun(Miles::Queue* queue, const char* eventName)
{
	if(miles_debug.GetBool())
		Msg(eDLL_T::AUDIO, "%s: running event: '%s'\n", __FUNCTION__, eventName);

	v_MilesQueueEventRun(queue, eventName);
}

//-----------------------------------------------------------------------------
// Purpose: patches miles banks
//-----------------------------------------------------------------------------
void MilesBankPatch(Miles::Bank* bank, char* streamPatch, char* localizedStreamPatch)
{
	if (miles_debug.GetBool())
	{
		Msg(eDLL_T::AUDIO,
			"%s: patching bank \"%s\". stream patches: \"%s\", \"%s\"\n",
			__FUNCTION__,
			bank->GetBankName(),
			V_UnqualifiedFileName(streamPatch), V_UnqualifiedFileName(localizedStreamPatch)
		);
	}

	const Miles::BankHeader_t* header = bank->GetHeader();

	if (header->bankIndex >= header->project->bankCount)
		Error(eDLL_T::AUDIO, EXIT_FAILURE,
			"%s: attempted to patch bank \"%s\" that identified itself as bank #%i, project expects a highest index of #%i\n",
			__FUNCTION__,
			bank->GetBankName(),
			header->bankIndex,
			header->project->bankCount - 1
		);

	v_MilesBankPatch(bank, streamPatch, localizedStreamPatch);
}

// Forward declarations
void CacheEventNameHash(const char* eventName, uint64_t hash);
void CacheEventNameByHash(const char* eventName, uint64_t hash);
const char* LookupEventNameFromHash(uint64_t hash);
static uint64_t ComputeMilesEventHash(const char* eventName);

//-----------------------------------------------------------------------------
// Purpose: adds an audio event to the queue (same function as Miles_SetEventHashFromName)
// Note: This IS the hash lookup function - it sets the event pointer for dispatch
//-----------------------------------------------------------------------------
static void CSOM_AddEventToQueue(const char* eventName)
{
	// Call original - this sets the event pointer in qword_1655B9658
	v_CSOM_AddEventToQueue(eventName);

	// Memory barrier to ensure the event hash is fully written before we read it
	// and before Miles processes the event. This fixes weapon sounds not playing
	// when miles_debug is disabled (the Msg() call was acting as an implicit barrier).
	_mm_mfence();

	// Cache the event pointer -> name mapping for later lookup
	if (eventName && *eventName && g_Miles_QueuedEventHash)
	{
		uint64_t eventPtr = *g_Miles_QueuedEventHash;
		if (eventPtr > 2)
		{
			CacheEventNameHash(eventName, eventPtr);
			if (fmod_debug->GetBool())
				Msg(eDLL_T::AUDIO, "FMOD: Cached eventPtr 0x%llX -> '%s'\n", eventPtr, eventName);
		}
		else if (eventPtr == 2 && be)
		{
			// Miles says "not found" - check if FMOD has this event
			if (be->EventExists(eventName))
			{
				// Get position from Miles globals (may be set even for unknown events)
				Vector3D soundPos = { 0.0f, 0.0f, 0.0f };
				if (g_Miles_SoundPosition)
				{
					float* posBase = g_Miles_SoundPosition;
					soundPos.x = *(posBase - 1);
					soundPos.y = posBase[0];
					soundPos.z = posBase[1];
				}
				if (soundPos == Vector3D{ 0.0f, 0.0f, 0.0f })
				{
					if (g_vecRenderOrigin)
						soundPos = *g_vecRenderOrigin;
				}
				
				if (fmod_debug->GetBool())
					Msg(eDLL_T::AUDIO, "FMOD: Playing FMOD-only event '%s' at (%.1f, %.1f, %.1f)\n", 
						eventName, soundPos.x, soundPos.y, soundPos.z);
				
				be->PlayEvent3D(eventName, soundPos, 1.0f, 0);
			}
		}
	}

	if (miles_debug.GetBool())
	{
		Msg(eDLL_T::AUDIO, "%s: queuing audio event '%s'\n", __FUNCTION__, eventName);
	}
	else
	{
		// Yield to other threads - Miles apparently needs a small window to process
		// the event internally. Without this (or the Msg() call above), weapon sounds
		// may not play. This replicates the timing effect of the debug print.
		SwitchToThread();
	}

	if (miles_warnings.GetBool())
	{
		uint64_t hash = g_Miles_QueuedEventHash ? *g_Miles_QueuedEventHash : 0;
		if (hash == 1)
			Warning(eDLL_T::AUDIO, "%s: failed to add event to queue; invalid event name '%s'\n", __FUNCTION__, eventName);

		if (hash == 2 && (!be || !be->EventExists(eventName)))  // Only warn if FMOD doesn't have it either
			Warning(eDLL_T::AUDIO, "%s: failed to add event to queue; event '%s' not found.\n", __FUNCTION__, eventName);
	}
};

static void ProcessClientAnimEvent(__int64 a1, __int64 a2, __int64 a3, unsigned int a4, const char* a5, __int64 a6, __int64 a7)
{
	//if (fmod_debug->GetBool())
	//{
		//Msg(eDLL_T::AUDIO, "ProcessClientAnimEvent: a4 = %d || a5 = %s\n", a4, a5);
	//}

	v_ProcessClientAnimEvent(a1, a2, a3, a4, a5, a6, a7);
}

// NOTE: Individual emit/stop hooks removed - Miles_DispatchEvent_Hook catches ALL audio events

void FmodDebugEventPrint(const char* eventType, const char* eventName)
{
	if(fmod_debug->GetBool())
	{
		Msg(eDLL_T::AUDIO, "FMOD: %s: '%s'\n", eventType, eventName);
	}
}

// NOTE: EmitSoundOnEntityImpl_Hook and Charge_EmitSoundOnEntityForLocalPlayer removed - dispatcher catches all

//-----------------------------------------------------------------------------
// Purpose: Helper to reverse lookup event name from hash using cached mapping
// Note: The value at qword_1655B9658 is a pointer to event data (entry + 0x10)
//       The actual FNV-1a hash is stored at (eventPtr - 8)
//-----------------------------------------------------------------------------
static std::unordered_map<uint64_t, std::string> g_hashToEventName;
static std::mutex g_hashMapMutex;

//-----------------------------------------------------------------------------
// Purpose: Compute FNV-1a hash the same way Miles does (lowercase, . -> _)
//-----------------------------------------------------------------------------
static uint64_t ComputeMilesEventHash(const char* eventName)
{
	if (!eventName || !*eventName) return 1; // Empty name returns 1

	uint64_t hash = 0xCBF29CE484222325ULL; // FNV-1a 64-bit offset basis
	const char* p = eventName;

	while (*p)
	{
		char c = *p;
		// Convert to lowercase
		if (c >= 'A' && c <= 'Z')
			c += 32;
		// Replace . with _
		else if (c == '.')
			c = '_';

		hash ^= static_cast<uint8_t>(c);
		hash *= 0x100000001B3ULL; // FNV-1a 64-bit prime
		++p;
	}

	return hash;
}

// Helper to get the actual FNV-1a hash from an event pointer
// The eventPtr (qword_1655B9658) points to entry + 0x08, which IS the hash
inline uint64_t GetRealHashFromEventPtr(uint64_t eventPtr)
{
	// Values below a reasonable pointer threshold are likely raw hashes/IDs
	// User-mode addresses on x64 Windows are typically >= 0x10000
	if (eventPtr < 0x10000) return eventPtr;
	// Event pointer points directly to the hash at entry + 0x08
	return *reinterpret_cast<uint64_t*>(eventPtr);
}

void CacheEventNameHash(const char* eventName, uint64_t eventPtr)
{
	if (!eventName || !*eventName || eventPtr <= 2) return;
	uint64_t realHash = GetRealHashFromEventPtr(eventPtr);
	std::lock_guard<std::mutex> lock(g_hashMapMutex);
	g_hashToEventName[realHash] = eventName;
}

// Cache using a pre-computed hash directly (for FMOD event enumeration)
void CacheEventNameByHash(const char* eventName, uint64_t hash)
{
	if (!eventName || !*eventName || hash <= 2) return;
	std::lock_guard<std::mutex> lock(g_hashMapMutex);
	g_hashToEventName[hash] = eventName;
}

const char* LookupEventNameFromHash(uint64_t eventPtr)
{
	if (eventPtr <= 2) return nullptr;
	uint64_t realHash = GetRealHashFromEventPtr(eventPtr);
	std::lock_guard<std::mutex> lock(g_hashMapMutex);
	auto it = g_hashToEventName.find(realHash);
	if (it != g_hashToEventName.end())
		return it->second.c_str();
	return nullptr;
}

// Lookup by raw FNV-1a hash (for pre-computed hash lookups)
const char* LookupEventNameByRawHash(uint64_t rawHash)
{
	if (rawHash <= 2) return nullptr;
	std::lock_guard<std::mutex> lock(g_hashMapMutex);
	auto it = g_hashToEventName.find(rawHash);
	if (it != g_hashToEventName.end())
		return it->second.c_str();
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Enumerate all Miles events from the event table and cache their names
// This must be called AFTER Miles initialization is complete (event table is built)
//-----------------------------------------------------------------------------
static void CacheMilesEventNames()
{
	// Check if we have the required globals
	if (!g_Miles_EventTableBase)
	{
		Warning(eDLL_T::AUDIO, "FMOD: Cannot enumerate Miles events - g_Miles_EventTableBase pointer is null\n");
		return;
	}
	
	if (!*g_Miles_EventTableBase)
	{
		Warning(eDLL_T::AUDIO, "FMOD: Cannot enumerate Miles events - event table not built yet (value=0)\n");
		return;
	}

	if (!g_Miles_BankHandle)
	{
		Warning(eDLL_T::AUDIO, "FMOD: Cannot enumerate Miles events - g_Miles_BankHandle pointer is null\n");
		return;
	}
	
	if (!*g_Miles_BankHandle)
	{
		Warning(eDLL_T::AUDIO, "FMOD: Cannot enumerate Miles events - bank handle not set yet (value=0)\n");
		return;
	}

	if (!g_Miles_HashBucketTable)
	{
		Warning(eDLL_T::AUDIO, "FMOD: Cannot enumerate Miles events - g_Miles_HashBucketTable pointer is null\n");
		return;
	}

	uint64_t tableBase = *g_Miles_EventTableBase;
	uint64_t bankHandle = *g_Miles_BankHandle;

	int eventsCached = 0;
	int eventsNewlyCached = 0;

	// Iterate through all hash buckets
	for (int bucket = 0; bucket < 4096; bucket++)
	{
		if (!g_Miles_HashBucketTable)
			break;

		uint32_t entryIndex = g_Miles_HashBucketTable[bucket];
		
		// Follow the chain of entries in this bucket
		while (entryIndex != 0)
		{
			// Calculate entry address: tableBase + entryIndex * 48
			uint64_t entryAddr = tableBase + static_cast<uint64_t>(entryIndex) * 48;
			
			// Read the entry structure
			uint32_t nextIndex = *reinterpret_cast<uint32_t*>(entryAddr);        // Offset 0x00
			uint64_t fullHash = *reinterpret_cast<uint64_t*>(entryAddr + 0x08);  // Offset 0x08
			
			if (fullHash > 2)
			{
				// Check if we already have this hash cached
				const char* existing = LookupEventNameByRawHash(fullHash);
				if (!existing)
				{
					// Try to get the event name using MilesEventGetDetails
					// The template ID pointer is at entryAddr + 0x10
					// MilesEventGetDetails expects a pointer to the templateId location
					const char* eventName = nullptr;
					int param1 = 0, param2 = 0, param3 = 0;
					
					__int64* templateIdPtr = reinterpret_cast<__int64*>(entryAddr + 0x10);
					
					// Call with 7 params - the shim layer handles compatibility
					if (v_MilesEventGetDetails && v_MilesEventGetDetails(
						bankHandle, 
						reinterpret_cast<__int64>(templateIdPtr), 
						reinterpret_cast<__int64>(&eventName), 
						reinterpret_cast<__int64>(&param1), 
						reinterpret_cast<__int64>(&param2), 
						reinterpret_cast<__int64>(&param3),
						nullptr))
					{
						if (eventName && *eventName)
						{
							// Cache with Miles' actual hash from the table (what gets passed at runtime)
							CacheEventNameByHash(eventName, fullHash);
							eventsNewlyCached++;
							
							if (fmod_debug->GetBool())
								Msg(eDLL_T::AUDIO, "FMOD: Cached Miles event '%s' (hash=0x%llX)\n", eventName, fullHash);
						}
					}
				}
				else
				{
					eventsCached++;
				}
			}
			
			// Move to next entry in chain
			entryIndex = nextIndex;
		}
	}

	if (fmod_debug->GetBool())
	{
		Msg(eDLL_T::AUDIO, "FMOD: Miles event enumeration complete - %d already cached, %d newly cached\n", 
			eventsCached, eventsNewlyCached);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Core event dispatcher hook - catches ALL Miles audio events
// Input  : action - 1=play, 2=stop, 3=other
//-----------------------------------------------------------------------------
static __int64 Miles_DispatchEvent_Hook(unsigned __int16 action)
{
	// Deferred Miles event enumeration - table is now built
	if (!g_milesEventsCached)
	{
		if (fmod_debug->GetBool())
		{
			Msg(eDLL_T::AUDIO, "FMOD: Checking Miles event table... g_Miles_EventTableBase=%p, value=%llX\n", 
				(void*)g_Miles_EventTableBase, g_Miles_EventTableBase ? *g_Miles_EventTableBase : 0);
		}
		
		if (g_Miles_EventTableBase && *g_Miles_EventTableBase)
		{
			g_milesEventsCached = true;
			CacheMilesEventNames();
		}
	}

	// Get the queued event hash from the correct global (qword_1655B9658)
	uint64_t eventHash = g_Miles_QueuedEventHash ? *g_Miles_QueuedEventHash : 0;
	
	// Debug: always log what we're seeing
	if (fmod_debug->GetBool())
	{
		const char* cachedName = (eventHash > 2) ? LookupEventNameFromHash(eventHash) : nullptr;
		if (fmod_debug->GetBool())
		{
			Msg(eDLL_T::AUDIO, "FMOD: Miles_DispatchEvent action=%d hash=0x%llX name='%s'\n", 
				action, eventHash, cachedName ? cachedName : "(unknown)");
		}
	}
	
	// Ignore invalid hashes (1=empty name, 2=not found)
	if (eventHash > 2 && be)
	{
		// Try to find cached event name for this hash
		const char* cachedName = LookupEventNameFromHash(eventHash);
		
		if (action == 1) // PLAY
		{
			if (cachedName && be->EventExists(cachedName))
			{
				// Skip dialogue events and events marked as dont_override
				if (V_strstr(cachedName, "diag") || be->GetUserPropertyBool(cachedName, "dont_override"))
				{
					// Let Miles handle these
					return v_Miles_DispatchEvent(action);
				}
				
				// Get position from Miles globals
				// Layout: qword_1655B9660 high=X, qword_1655B9668 low=Y high=Z
				Vector3D soundPos = { 0.0f, 0.0f, 0.0f };
				if (g_Miles_SoundPosition)
				{
					// g_Miles_SoundPosition points to qword_1655B9668
					// X is at qword_1655B9660 + 4 (previous qword's high dword)
					float* posBase = g_Miles_SoundPosition;
					float posX = *(posBase - 1);  // HIDWORD of qword_1655B9660
					float posY = posBase[0];       // LODWORD of qword_1655B9668
					float posZ = posBase[1];       // HIDWORD of qword_1655B9668
					soundPos = { posX, posY, posZ };
				}
				
				// Debug: log the position we're using
				if (fmod_debug->GetBool())
				{
					Msg(eDLL_T::AUDIO, "FMOD: Sound position: (%.1f, %.1f, %.1f)\n", 
						soundPos.x, soundPos.y, soundPos.z);
				}
				
				// If position is zero, use player position as fallback
				if (soundPos == Vector3D{ 0.0f, 0.0f, 0.0f })
				{
					if (g_vecRenderOrigin)
						soundPos = *g_vecRenderOrigin;
					if (fmod_debug->GetBool())
						Msg(eDLL_T::AUDIO, "FMOD: Using player pos fallback: (%.1f, %.1f, %.1f)\n", 
							soundPos.x, soundPos.y, soundPos.z);
				}
				
				FmodDebugEventPrint("Miles_DispatchEvent PLAY (override)", cachedName);
				be->PlayEvent3D(cachedName, soundPos, 1.0f, eventHash);
				
				// Block Miles from playing its version - we're overriding
				return 0;
			}
		}
		else if (action == 2) // STOP
		{
			// Stop the corresponding FMOD instance
			be->StopEventByHash(eventHash, false);
		}
	}
	
	// Call original for events we're not overriding
	return v_Miles_DispatchEvent(action);
}

//-----------------------------------------------------------------------------\n// NOTE: Miles_SetEventHashFromName_Hook removed - same function as CSOM_AddEventToQueue\n// The caching is now done in CSOM_AddEventToQueue hook above\n//-----------------------------------------------------------------------------\n\n//-----------------------------------------------------------------------------
// Purpose: Hook for direct hash-based event lookup (sub_14095B420)
// This catches events that use pre-computed hashes instead of string names
//-----------------------------------------------------------------------------
static void Miles_SetEventHashFromValue_Hook(__int64 preComputedHash)
{
	// Try to find cached name for this hash BEFORE the lookup
	const char* cachedName = LookupEventNameByRawHash(preComputedHash);
	
	// Call original - this will set eventPtr in g_Miles_QueuedEventHash
	v_Miles_SetEventHashFromValue(preComputedHash);
	
	// If we found a cached name, log success
	if (cachedName && g_Miles_QueuedEventHash)
	{
		uint64_t eventPtr = *g_Miles_QueuedEventHash;
		if (eventPtr > 2)
		{
			if (fmod_debug->GetBool())
				Msg(eDLL_T::AUDIO, "FMOD: HashLookup: hash=0x%llX -> '%s'\n", preComputedHash, cachedName);
		}
	}
	else if (g_Miles_QueuedEventHash && *g_Miles_QueuedEventHash > 2)
	{
		// Hash lookup succeeded in Miles but we don't have it cached - try to cache it now
		uint64_t eventPtr = *g_Miles_QueuedEventHash;
		
		// Try to get the event name using MilesEventGetDetails
		// eventPtr points to entry + 0x08, so template ID is at eventPtr + 0x08
		if (g_Miles_BankHandle && *g_Miles_BankHandle && v_MilesEventGetDetails)
		{
			const char* eventName = nullptr;
			int param1 = 0, param2 = 0, param3 = 0;
			
			__int64* templateIdPtr = reinterpret_cast<__int64*>(eventPtr + 0x08);
			
			if (v_MilesEventGetDetails(
				*g_Miles_BankHandle,
				reinterpret_cast<__int64>(templateIdPtr),
				reinterpret_cast<__int64>(&eventName),
				reinterpret_cast<__int64>(&param1),
				reinterpret_cast<__int64>(&param2),
				reinterpret_cast<__int64>(&param3),
				nullptr))
			{
				if (eventName && *eventName)
				{
					// Cache by preComputedHash so next lookup succeeds
					CacheEventNameByHash(eventName, preComputedHash);
					if (fmod_debug->GetBool())
						Msg(eDLL_T::AUDIO, "FMOD: HashLookup (late cache): hash=0x%llX -> '%s'\n", preComputedHash, eventName);
					return;
				}
			}
		}
		
		if (fmod_debug->GetBool())
			Msg(eDLL_T::AUDIO, "FMOD: HashLookup: hash=0x%llX -> (uncached)\n", preComputedHash);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Stop event by hash signal hook - catches signal-based stops
//-----------------------------------------------------------------------------
static __int64 Miles_StopEventByHash_Hook(__int64 eventHash)
{
	if (eventHash > 2 && be)
	{
		const char* cachedName = LookupEventNameFromHash(eventHash);
		if (fmod_debug->GetBool())
		{
			if (cachedName)
				Msg(eDLL_T::AUDIO, "FMOD: Miles_StopEventByHash: '%s' (hash=0x%llX)\n", cachedName, eventHash);
			else
				Msg(eDLL_T::AUDIO, "FMOD: Miles_StopEventByHash: (hash=0x%llX)\n", eventHash);
		}
		
		be->StopEventByHash(eventHash, false);
	}
	
	return v_Miles_StopEventByHash(eventHash);
}

//-----------------------------------------------------------------------------
// Purpose: Stop all sounds hook - stops all FMOD sounds too
//-----------------------------------------------------------------------------
static __int64 Miles_StopAllSounds_Hook(__int64 a1, __int64 a2, __int64 filterType)
{
	if (be)
	{
		if (fmod_debug->GetBool())
			Msg(eDLL_T::AUDIO, "FMOD: Miles_StopAllSounds (filter=%lld)\n", filterType);
		
		// Stop all FMOD sounds immediately when Miles stops all
		be->StopAll(true);
	}
	
	return v_Miles_StopAllSounds(a1, a2, filterType);
}


//-----------------------------------------------------------------------------
// Purpose: close and reset the CSOM async file instance
//-----------------------------------------------------------------------------
static void CSOM_CloseAsyncFile(CSOM_AsyncFile_s* const asyncFile)
{
	asyncFile->asyncRequestId = 0;
	asyncFile->fileName[0] = '\0';
	FS_CloseAsyncFile(asyncFile->fileHandle);
	asyncFile->fileHandle = FS_ASYNC_FILE_INVALID;
	asyncFile->readOffset = 0;
	asyncFile->fileSize = 0;
}

//-----------------------------------------------------------------------------
// Structure for each live file instance
//-----------------------------------------------------------------------------
struct CSOM_FileInfo_s
{
	char fileName[256];
	size_t fileSize;
	int fileHandle;
};

#define CSOM_MAX_OPENED_FILES 32

static CSOM_FileInfo_s s_milesFileInfos[CSOM_MAX_OPENED_FILES];
static size_t s_numMilesFilesOpened = 0;

//-----------------------------------------------------------------------------
// Purpose: finds the file handle for given name, opens it if not found
//-----------------------------------------------------------------------------
static int CSOM_MilesAsync_OpenOrFindFile(const char* const fileName, size_t& outFileSize)
{
	if (s_numMilesFilesOpened)
	{
		// Find the file.
		CSOM_FileInfo_s* infoIt = s_milesFileInfos;
		size_t currIdx = 0;
		bool notFound = false; // If true, will try and open the file.

		while (V_strcmp(fileName, infoIt->fileName))
		{
			++currIdx;
			++infoIt;

			if (currIdx == s_numMilesFilesOpened)
			{
				notFound = true;
				break;
			}
		}

		if (!notFound)
		{
			outFileSize = infoIt->fileSize;
			g_pakLoadApi->IncrementAsyncFileRefCount(infoIt->fileHandle);

			return infoIt->fileHandle;
		}
	}

	if (s_numMilesFilesOpened == CSOM_MAX_OPENED_FILES)
		return FS_ASYNC_FILE_INVALID; // Max opened files reached.

	// Open the file.
	CSOM_FileInfo_s* const info = &s_milesFileInfos[s_numMilesFilesOpened++];
	V_strncpy(info->fileName, fileName, sizeof(info->fileName));

	info->fileHandle = FS_OpenAsyncFile(fileName, 4, &info->fileSize);

	if (info->fileHandle == FS_ASYNC_FILE_INVALID)
		Error(eDLL_T::AUDIO, EXIT_FAILURE, "%s( \"%s\" ) failed to open file; try resyncing\n", __FUNCTION__, fileName);

	outFileSize = info->fileSize;
	g_pakLoadApi->IncrementAsyncFileRefCount(info->fileHandle);

	return info->fileHandle;
}

//-----------------------------------------------------------------------------
// Purpose: returns the first free file slot index
//-----------------------------------------------------------------------------
static inline size_t CSOM_MilesAsync_GetFirstFreeFileSlot()
{
	size_t index = 0;

	// Scan the list.
	while (g_milesGlobals->asyncFiles[index].asyncRequestId)
		index++;

	return index;
}

//-----------------------------------------------------------------------------
// User structure for MilesAsyncRead
//-----------------------------------------------------------------------------
struct CSOM_AsyncRead_s
{
	int asyncFileHandle;
	bool shouldCloseFile;
	bool readFinished;
};

//-----------------------------------------------------------------------------
// Purpose: Miles async file read request handler; maps to internal callback of
//          MilesAsyncFileRead, set through API MilesAsyncSetCallbacks.
//-----------------------------------------------------------------------------
static s32 CSOM_MilesAsync_FileRead(MilesAsyncRead* const request)
{
	CSOM_AsyncRead_s* const user = (CSOM_AsyncRead_s*)request->Internal;
	CSOM_AsyncFile_s* asyncFile;

	if (request->RequestId)
	{
		asyncFile = &g_milesGlobals->asyncFiles[request->RequestId & CSOM_MAX_ASYNC_FILE_HANDLES_MASK];
	}
	else // New request, open the file.
	{
		const size_t asyncFileIdx = CSOM_MilesAsync_GetFirstFreeFileSlot();
		asyncFile = &g_milesGlobals->asyncFiles[asyncFileIdx];

		MilesSubFileInfo_s sfi; char fileNameStack[512];
		MilesGetSubFileInfo(fileNameStack, request->FileName, &sfi);

		R_UTF8_strncpy(asyncFile->fileName, sfi.filename, sizeof(asyncFile->fileName));

		asyncFile->fileSize = 0;
		asyncFile->fileHandle = CSOM_MilesAsync_OpenOrFindFile(sfi.filename, asyncFile->fileSize);

		asyncFile->readOffset = 0;
		asyncFile->readStart = sfi.start;

		if (sfi.size)
		{
			const size_t subFileSize = sfi.size + sfi.start;

			if (subFileSize < asyncFile->fileSize)
				asyncFile->fileSize = subFileSize;
		}

		// Give the request an unique ID with its slot index packed into it.
		const u64 asyncRequestId = asyncFileIdx + (++g_milesGlobals->asyncRequestIdGen * CSOM_MAX_ASYNC_FILE_HANDLES);

		asyncFile->asyncRequestId = asyncRequestId;
		request->RequestId = asyncRequestId;
	}

	user->shouldCloseFile = (request->Flags & MSSIO_FLAGS_DONT_CLOSE_HANDLE) == 0;

	if ((request->Flags & (MSSIO_FLAGS_QUERY_START_ONLY|MSSIO_FLAGS_QUERY_SIZE_ONLY)) != 0)
		request->Start = asyncFile->fileSize - asyncFile->readStart;

	size_t readCount = request->Count;

	if ((request->Flags & MSSIO_FLAGS_QUERY_SIZE_ONLY) != 0 || readCount == 0)
	{
		user->asyncFileHandle = FS_ASYNC_FILE_INVALID;
		request->Status = MSSIO_STATUS_COMPLETE;

		if (user->shouldCloseFile)
			CSOM_CloseAsyncFile(asyncFile);

		return 1;
	}

	size_t readOffset = 0;

	if ((request->Flags & MSSIO_FLAGS_DONT_USE_OFFSET) == 0)
	{
		readOffset = request->Offset;
		asyncFile->readOffset = readOffset;
	}

	if (readCount < 0)
	{
		readCount = asyncFile->fileSize - asyncFile->readStart - readOffset;
		request->Count = readCount;
	}

	size_t numBytesLeft = asyncFile->fileSize - asyncFile->readStart - readOffset;

	if (readCount < numBytesLeft)
		numBytesLeft = readCount;

	request->Count = numBytesLeft;

	if (!request->Buffer)
	{
		// Allocate a read buffer.
		const size_t readBufSize = numBytesLeft + request->ReadAmt;
		void* const readBuffer = v_MilesAllocEx(readBufSize, 0, g_milesGlobals->driver, request->LastAllocSrcFileName, request->LastAllocSrcFileLine);

		if (!readBuffer)
		{
			Error(eDLL_T::AUDIO, EXIT_FAILURE, "Miles async failed malloc for '%s' size %zu\n", asyncFile->fileName, readBufSize);

			user->asyncFileHandle = FS_ASYNC_FILE_INVALID;
			request->Status = MSSIO_STATUS_ERROR_MEMORY_ALLOC_FAIL;

			if (user->shouldCloseFile)
				CSOM_CloseAsyncFile(asyncFile);

			return 0;
		}

		request->Buffer = readBuffer;
	}

	if (request->Count)
	{
		// Read data into the buffer.
		user->readFinished = false;
		user->asyncFileHandle = g_pakLoadApi->ReadAsyncFile(asyncFile->fileHandle, asyncFile->readStart + asyncFile->readOffset, request->Count, request->Buffer, 1);

		if (user->asyncFileHandle == FS_ASYNC_FILE_INVALID)
		{
			Error(eDLL_T::AUDIO, EXIT_FAILURE, "Miles async failed read for '%s' offset %zu count %zu\n", asyncFile->fileName, request->Offset, request->Count);
			request->Status = MSSIO_STATUS_ERROR_FAILED_OPEN;

			if (user->shouldCloseFile)
				CSOM_CloseAsyncFile(asyncFile);

			return 0;
		}

		request->Status = MSSIO_STATUS_COMPLETE_NOP;
	}
	else
	{
		request->Status = MSSIO_STATUS_COMPLETE_NOP;
		user->readFinished = true;
		user->asyncFileHandle = FS_ASYNC_REQ_INVALID;
	}

	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: Miles async file status request handler; maps to internal callback
//          of MilesAsyncFileStatus, set through API MilesAsyncSetCallbacks.
//-----------------------------------------------------------------------------
static s32 CSOM_MilesAsync_FileStatus(MilesAsyncRead* const request, const u32 i_MS)
{
	CSOM_AsyncRead_s* const user = (CSOM_AsyncRead_s*)request->Internal;

	if (user->asyncFileHandle == FS_ASYNC_FILE_INVALID)
		return request->Status;

	AsyncHandleStatus_s::Status_e currentStatus;

	if (user->readFinished)
	{
		currentStatus = AsyncHandleStatus_s::Status_e::FS_ASYNC_READY;
	}
	else
	{
		if (i_MS)
			g_pakLoadApi->WaitForAsyncRequest(user->asyncFileHandle);

		currentStatus = g_pakLoadApi->CheckAsyncRequest(user->asyncFileHandle, nullptr, nullptr);

		if (currentStatus == AsyncHandleStatus_s::Status_e::FS_ASYNC_PENDING)
			return 0;
	}

	user->asyncFileHandle = FS_ASYNC_FILE_INVALID;
	CSOM_AsyncFile_s* const asyncFile = &g_milesGlobals->asyncFiles[request->RequestId & CSOM_MAX_ASYNC_FILE_HANDLES_MASK];

	if (user->shouldCloseFile)
		CSOM_CloseAsyncFile(asyncFile);

	if (currentStatus == AsyncHandleStatus_s::Status_e::FS_ASYNC_READY)
	{
		request->LastCount = request->Count;
		asyncFile->readOffset += request->Count;
		request->Status = MSSIO_STATUS_COMPLETE;
	}
	else // Failure or canceled.
	{
		request->LastCount = 0;

		if (currentStatus == AsyncHandleStatus_s::Status_e::FS_ASYNC_CANCELLED)
			request->Status = MSSIO_STATUS_ERROR_CANCELLED;
		else
			request->Status = MSSIO_STATUS_ERROR_FAILED_READ;
	}

	return request->Status;
}

//-----------------------------------------------------------------------------
// Purpose: Miles async file cancel request handler; maps to internal callback
//          of MilesAsyncFileCancel, set through API MilesAsyncSetCallbacks.
//-----------------------------------------------------------------------------
static s32 CSOM_MilesAsync_FileCancel(MilesAsyncRead* const request)
{
	CSOM_AsyncRead_s* const user = (CSOM_AsyncRead_s*)request->Internal;

	if (user->asyncFileHandle == FS_ASYNC_FILE_INVALID)
		return 1; // Nothing to cancel.

	if (!user->readFinished)
		g_pakLoadApi->CancelAsyncRequest(user->asyncFileHandle);

	return CSOM_MilesAsync_FileStatus(request, RR_WAIT_INFINITE);
}

///////////////////////////////////////////////////////////////////////////////
void MilesCore::Detour(const bool bAttach) const
{
	//DetourSetup(&v_MilesEventBuild, &MilesEventBuild_Hook, bAttach);
	DetourSetup(&v_MilesQueueEventRun, &MilesQueueEventRun, bAttach);
	//DetourSetup(&v_MilesBankPatch, &MilesBankPatch, bAttach);
	DetourSetup(&v_CSOM_Initialize, &CSOM_Initialize, bAttach);
	DetourSetup(&v_CSOM_InitializeBankList, &CSOM_InitializeBankList, bAttach);
	DetourSetup(&v_CSOM_LogFunc, &CSOM_LogFunc, bAttach);
	DetourSetup(&v_CSOM_MilesAsync_FileRead, &CSOM_MilesAsync_FileRead, bAttach);
	DetourSetup(&v_CSOM_MilesAsync_FileStatus, &CSOM_MilesAsync_FileStatus, bAttach);
	DetourSetup(&v_CSOM_MilesAsync_FileCancel, &CSOM_MilesAsync_FileCancel, bAttach);
	DetourSetup(&v_CSOM_AddEventToQueue, &CSOM_AddEventToQueue, bAttach);  // This is also the hash lookup function
	DetourSetup(&v_ProcessClientAnimEvent, &ProcessClientAnimEvent, bAttach);

	// Core Miles audio event hooks - catch ALL audio events at the source
	if (v_Miles_DispatchEvent)
		DetourSetup(&v_Miles_DispatchEvent, &Miles_DispatchEvent_Hook, bAttach);
	// NOTE: Miles_SetEventHashFromName is the same function as CSOM_AddEventToQueue, already hooked above
	if (v_Miles_SetEventHashFromValue)
		DetourSetup(&v_Miles_SetEventHashFromValue, &Miles_SetEventHashFromValue_Hook, bAttach);
	if (v_Miles_StopEventByHash)
		DetourSetup(&v_Miles_StopEventByHash, &Miles_StopEventByHash_Hook, bAttach);
	if (v_Miles_StopAllSounds)
		DetourSetup(&v_Miles_StopAllSounds, &Miles_StopAllSounds_Hook, bAttach);

	if (bAttach)
	{
		CMemory mem(v_CSOM_RunFrame);

		// Between Miles version 10.0.48 and 10.0.50, they swapped locations of
		// 2 members in a struct returned by MilesEventInfoQueueEnum on type 4.
		// This change breaks closed captions (sub-titles). The fix is to apply
		// the swap in the assembly code as well so the engine retrieves the
		// values correctly from the new locations again. The structure layout
		// on all other enums are still identical and do not need to be fixes.
		mem.Offset(0x762).Patch({ 0x4 });
		mem.Offset(0x78B).Patch({ 0xC });

		// Miles 10.0.62 changed internal listener data structures causing a crash
		// when the audio spatialization function tries to clear listener bitmask bits.
		// The crash occurs at offset 0x4A2 with instruction: sub [rbx+r11*8], rax
		// 
		// The bug is that the old code does a direct memory subtraction which can cause
		// issues. The fix (from newer Miles versions) is to:
		//   1. Subtract from the register copy (R9) instead of memory
		//   2. Write the result back to memory
		//
		// Original (buggy):   sub [rbx+r11*8], rax      ; 4A 29 04 DB
		// Fixed:              sub r9, rax               ; 49 29 C1
		//                     mov [rbx+r11*8], r9       ; 4E 89 0C DB
		//
		// Since the fixed code is larger (7 bytes vs 4 bytes), we use a code cave
		// (trampoline) to hold the fixed instructions and redirect execution there.
		// IMPORTANT: The code cave MUST be allocated within ±2GB of the target to use
		// rel32 jumps, so we search for free memory near the function.
		if (v_Miles_ProcessListenerMasks)
		{
			CMemory listenerMem(v_Miles_ProcessListenerMasks);
			static uint8_t* s_listenerMaskCodeCave = nullptr;
			if (!s_listenerMaskCodeCave)
			{
				/*// We need to allocate executable memory within ±2GB of the patch site
				// to use 32-bit relative jumps. Search for a free region near the function.
				const uintptr_t targetAddr = listenerMem.Offset(0x4A2).GetPtr();
				const SIZE_T allocSize = 64;
				
				// Try to allocate within ±2GB range (start searching 1GB before target)
				uintptr_t searchStart = (targetAddr > 0x40000000) ? (targetAddr - 0x40000000) : 0x10000;
				uintptr_t searchEnd = targetAddr + 0x40000000;
				
				MEMORY_BASIC_INFORMATION mbi;
				for (uintptr_t addr = searchStart; addr < searchEnd; addr += mbi.RegionSize)
				{
					if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == 0)
					{
						addr += 0x10000;  // Skip on error
						continue;
					}
					
					if (mbi.State == MEM_FREE && mbi.RegionSize >= allocSize)
					{
						// Try to allocate at this address
						uintptr_t alignedAddr = (reinterpret_cast<uintptr_t>(mbi.BaseAddress) + 0xFFFF) & ~0xFFFF;
						s_listenerMaskCodeCave = reinterpret_cast<uint8_t*>(
							VirtualAlloc(reinterpret_cast<LPVOID>(alignedAddr), allocSize, 
							             MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
						
						if (s_listenerMaskCodeCave)
						{
							// Verify it's within rel32 range (±2GB)
							intptr_t distance = reinterpret_cast<intptr_t>(s_listenerMaskCodeCave) - 
							                    static_cast<intptr_t>(targetAddr);
							if (distance < INT32_MIN || distance > INT32_MAX)
							{
								// Too far, free and try again
								VirtualFree(s_listenerMaskCodeCave, 0, MEM_RELEASE);
								s_listenerMaskCodeCave = nullptr;
								continue;
							}
							break;  // Found suitable memory
						}
					}
				}*/
				
				/*if (s_listenerMaskCodeCave)
				{
					// Build the fixed code sequence in the code cave:
					// sub r9, rax         ; 49 29 C1 (3 bytes) - subtract from register
					// mov [rbx+r11*8], r9 ; 4E 89 0C DB (4 bytes) - write result to memory
					// jmp back_to_loop    ; E9 xx xx xx xx (5 bytes) - return to loop start
					
					s_listenerMaskCodeCave[0] = 0x49;  // REX.WB
					s_listenerMaskCodeCave[1] = 0x29;  // SUB r/m64, r64
					s_listenerMaskCodeCave[2] = 0xC1;  // ModR/M: r9, rax
					
					s_listenerMaskCodeCave[3] = 0x4E;  // REX.WRX
					s_listenerMaskCodeCave[4] = 0x89;  // MOV r/m64, r64
					s_listenerMaskCodeCave[5] = 0x0C;  // ModR/M: [rbx+r11*8]
					s_listenerMaskCodeCave[6] = 0xDB;  // SIB: scale=8, index=r11, base=rbx
					
					// Calculate jump offset back to loop start (offset 0x477 in the function)
					// The original jmp short at 0x4A6 jumps to 0x477 (the bsf instruction)
					const uintptr_t loopStartAddr = listenerMem.Offset(0x477).GetPtr();
					const uintptr_t jmpInstructionEnd = reinterpret_cast<uintptr_t>(&s_listenerMaskCodeCave[12]);
					const int32_t jmpOffset = static_cast<int32_t>(loopStartAddr - jmpInstructionEnd);
					
					s_listenerMaskCodeCave[7] = 0xE9;  // JMP rel32
					*reinterpret_cast<int32_t*>(&s_listenerMaskCodeCave[8]) = jmpOffset;
					
					// Now patch the original code at offset 0x4A2 to jump to our code cave
					// We have 6 bytes available (4 bytes crash instr + 2 bytes jmp short)
					// Patch with: jmp rel32 to codeCave (5 bytes) + nop (1 byte)
					const uintptr_t patchAddr = listenerMem.Offset(0x4A2).GetPtr();
					const uintptr_t caveAddr = reinterpret_cast<uintptr_t>(s_listenerMaskCodeCave);
					const int32_t caveOffset = static_cast<int32_t>(caveAddr - (patchAddr + 5));
					
					uint8_t patchBytes[6];
					patchBytes[0] = 0xE9;  // JMP rel32
					*reinterpret_cast<int32_t*>(&patchBytes[1]) = caveOffset;
					patchBytes[5] = 0x90;  // NOP (fills the remaining byte from old jmp short)
					
					listenerMem.Offset(0x4A2).Patch({ patchBytes[0], patchBytes[1], patchBytes[2], 
					                                   patchBytes[3], patchBytes[4], patchBytes[5] });
					
					Msg(eDLL_T::AUDIO, "Miles listener mask fix applied via code cave at %p (target: %p)\n", 
						static_cast<void*>(s_listenerMaskCodeCave), reinterpret_cast<void*>(targetAddr));
				}
				else*/
				{
					//Above is broken will fix later, for now it seems the simplest solution is to NOP out the crash instruction
					//Dosnt look like it has any adverse effects on audio
					listenerMem.Offset(0x4A2).Patch({ 0x90, 0x90, 0x90, 0x90 });
				}
			}
		}
	}
}
