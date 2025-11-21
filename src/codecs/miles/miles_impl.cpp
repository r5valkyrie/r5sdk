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

// Level change detection
static ConVar miles_wav_auto_stop_on_level_change("miles_wav_auto_stop_on_level_change", "1", FCVAR_RELEASE, "Automatically stop custom audio when level changes (1 = enabled, 0 = disabled)");

static ICustomAudioBackend* be = nullptr;

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
				if (fmod_debug.GetBool())
					Msg(eDLL_T::AUDIO, "Initialized FMOD Studio backend for custom audio\n");
			}
			else
			{
				if (fmod_debug.GetBool())
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

//-----------------------------------------------------------------------------
// Purpose: adds an audio event to the queue
//-----------------------------------------------------------------------------
static void CSOM_AddEventToQueue(const char* eventName)
{
	//if isnt a override and eventname dosnt contain dialogue
	if (!DoesOverrideExist(eventName))
	{
		v_CSOM_AddEventToQueue(eventName);

		if (miles_debug.GetBool())
		{
			Msg(eDLL_T::AUDIO, "%s: queuing audio event '%s'\n", __FUNCTION__, eventName);
		}

		if (miles_warnings.GetBool())
		{
			if (g_milesGlobals->queuedEventHash == 1)
				Warning(eDLL_T::AUDIO, "%s: failed to add event to queue; invalid event name '%s'\n", __FUNCTION__, eventName);

			if (g_milesGlobals->queuedEventHash == 2)
				Warning(eDLL_T::AUDIO, "%s: failed to add event to queue; event '%s' not found.\n", __FUNCTION__, eventName);
		}

		return;
	}

	if (DoesOverrideExist(eventName) && !V_strstr(eventName, "diag") && !be->GetUserPropertyBool(eventName, "dont_override"))
	{
		FmodDebugEventPrint("CSOM_AddEventToQueue Override", eventName);
		OverrideEventName(eventName);
		v_CSOM_AddEventToQueue("");
		return;
	}


	if(!be->GetUserPropertyBool(eventName, "dont_override"))
		FmodDebugEventPrint("CSOM_AddEventToQueue Error", eventName);

	v_CSOM_AddEventToQueue("");

};

static void ProcessClientAnimEvent(__int64 a1, __int64 a2, __int64 a3, unsigned int a4, const char* a5, __int64 a6, __int64 a7)
{
	//if (fmod_debug.GetBool())
	//{
		//Msg(eDLL_T::AUDIO, "ProcessClientAnimEvent: a4 = %d || a5 = %s\n", a4, a5);
	//}

	v_ProcessClientAnimEvent(a1, a2, a3, a4, a5, a6, a7);
}

Vector3D lastEntityOrigin = Vector3D{ 0.0f, 0.0f, 0.0f };
bool lastEntityOriginValid = false;

static int StopSoundOnEntityForLocalPlayer(__int64 a1, const char* eventName)
{
	if (be->EventExists(eventName))
	{
		FmodDebugEventPrint("StopSoundOnEntityForLocalPlayer", eventName);
		//be->StopSamplesForEvent(eventName);
	}

	return v_StopSoundOnEntityForLocalPlayer(a1, eventName);
}

static int EmitSoundOnEntityForLocalPlayer(__int64 a1, const char* eventName)
{
	if (be->EventExists(eventName))
	{
		if (V_strstr(eventName, "3p") || V_strstr(eventName, "3P"))
		{
			if (lastEntityOriginValid)
			{
				FmodDebugEventPrint("EmitSoundOnEntityForLocalPlayer 3P", eventName);
				be->PlayEvent3D(eventName, lastEntityOrigin, 1.0f);
				return 0;
			}
			else
			{
				FmodDebugEventPrint("EmitSoundOnEntityForLocalPlayer 3P No Origin", eventName);
				be->PlayEvent3D(eventName, { 0,0,0 }, 0.0f);
			}
		}
		else
		{
			FmodDebugEventPrint("EmitSoundOnEntityForLocalPlayer 1P", eventName);
			OverrideEventName(eventName);
			return 0;
		}
	}

	return v_EmitSoundOnEntityForLocalPlayer(a1, eventName);
}

static __int64 EmitSoundOnEntity(const char *a1, unsigned int a2, __int64 a3, const char *eventName, __int64 a5)
{
	if (V_strcmp(a1, "EmitSoundOnEntity") == 0)
	{
		if (be->EventExists(eventName))
		{
			if (V_strstr(eventName, "3p") || V_strstr(eventName, "3P"))
			{
				if (lastEntityOriginValid)
				{
					FmodDebugEventPrint("EmitSoundOnEntity 3P", eventName);
					be->PlayEvent3D(eventName, lastEntityOrigin, 1.0f);
					return 0;
				}
				else
				{
					FmodDebugEventPrint("EmitSoundOnEntity 3P No Origin", eventName);
					be->PlayEvent3D(eventName, { 0,0,0 }, 0.0f);
				}
			}
			else
			{
				FmodDebugEventPrint("EmitSoundOnEntity 1P", eventName);
				OverrideEventName(eventName);
				return 0;
			}
		}
	}

	FmodDebugEventPrint("EmitSoundOnEntity Default Call", eventName);
	return v_EmitSoundOnEntity(a1, a2, a3, eventName, a5);
}

void FmodDebugEventPrint(const char* eventType, const char* eventName)
{
	if(fmod_debug.GetBool())
	{
		Msg(eDLL_T::AUDIO, "FMOD: %s: '%s'\n", eventType, eventName);
	}
}

static __int64 EmitSoundOnEntityImpl_Hook(__int64 a1)
{
	// Hook for sub_1409B5120 (EmitSoundOnEntity VM entry)
	// Extract first argument value from VM frame
	// a1+88 -> base of value array; copy first value's 16 bytes (type + payload)
	__m128i val = *reinterpret_cast<const __m128i*>(*reinterpret_cast<const __int64*>(a1 + 88) + 16);

	// If null tag (matches decomp's 0x01000001) treat as no entity
	if (_mm_cvtsi128_si32(val) == 0x01000001)
	{
		return v_EmitSoundOnEntityImpl(a1);
	}

	// Resolve to entity using helper and VM entity type symbol
	__int64 entityPtr = 0;
	if (v_ResolveToEntity)
	{
		entityPtr = v_ResolveToEntity(a1, reinterpret_cast<__int64>(&val), reinterpret_cast<__int64>(g_VMEntityType));
	}

	// Try to get world origin from the resolved entity
	if (entityPtr)
	{
		const IClientEntity* const ent = reinterpret_cast<const IClientEntity*>(entityPtr);
		const Vector3D& origin = ent->GetAbsOrigin();
		lastEntityOrigin = origin;
		lastEntityOriginValid = true;
	}
	else
	{
		lastEntityOriginValid = false;
	}

	return v_EmitSoundOnEntityImpl(a1);
}

static int Charge_EmitSoundOnEntityForLocalPlayer(__int64 a1, const char* a2, float a3)
{
	if (be->EventExists(a2))
	{
		FmodDebugEventPrint("Charge_EmitSoundOnEntityForLocalPlayer Override", a2);
		OverrideEventName(a2);
		return 0;
	}

	FmodDebugEventPrint("Charge_EmitSoundOnEntityForLocalPlayer Default Call", a2);
	return v_Charge_EmitSoundOnEntityForLocalPlayer(a1, a2, a3);
}

void OverrideEventName(const char* eventName)
{
	Vector3D soundPos = g_milesGlobals->queuedSoundPosition;
	Vector3D playerPos = g_vecRenderOrigin ? *g_vecRenderOrigin : Vector3D{ 0.0f, 0.0f, 0.0f };

	if (soundPos == Vector3D{ 0.0f, 0.0f, 0.0f })
		soundPos = playerPos;

	be->PlayEvent3D(eventName, soundPos, 1.0f);
}

bool DoesOverrideExist(const char* eventName)
{
	if (be->EventExists(eventName))
	{
		return true;
	}
	else
	{
		std::atomic_thread_fence(std::memory_order_seq_cst);
		std::this_thread::yield();
		return false;
	}
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

/*//-----------------------------------------------------------------------------
// Purpose: Event build hook
//-----------------------------------------------------------------------------
// Global event name for Northstar hooks
// Event processing function - apply overrides after original function completes
static char __fastcall h_Sub_18002AAF0(__int64 a1, __int64 a2, __int64 a3, __int64 j)
{
	// Call original function first to let Miles set up structures
	char result = v_h_Sub_18002AAF0(a1, a2, a3, j);

	// Now safely apply overrides after Miles has finished processing
	__try
	{
		const char* name = **(const char***)(*(_QWORD*)(32i64 * *(unsigned __int16*)(a2 + 178) + *(_QWORD*)(a1 + 2216) + 8));

		const void* overridePtr = nullptr;
		unsigned overrideLen = 0;
		if (GetAudioOverrideManagerNorthstar()->TryGetOverrideForEvent(name, overridePtr, overrideLen))
		{
			if (wav_debug.GetBool())
				Msg(eDLL_T::AUDIO, "[NORTHSTAR] Applying override for event: %s (post-processing)\n", name);

			// Apply override after original function completed - should be safer
			__try
			{
				// Event structure cast (from decompiled code: v7 = (unsigned __int16 *)a2)
				unsigned __int16* eventStruct = (unsigned __int16*)a2;
				
				// Get first sample index from event structure (from decompiled: i = *v7)
				unsigned __int16 sampleIndex = *eventStruct;
				
				if (sampleIndex != 0xFFFF)  // Valid sample index
				{
					// Calculate sample structure address (from decompiled code)
					__int64 sampleArrayBase = *(_QWORD*)(a1 + 1872);
					__int64 sampleStructure = sampleArrayBase + 160i64 * sampleIndex;
					
					if (wav_debug.GetBool())
						Msg(eDLL_T::AUDIO, "[NORTHSTAR] Applying to sample structure at: %p (index %u)\n", 
							(void*)sampleStructure, sampleIndex);
					
					// Apply override after Miles processing is complete
					__try
					{
						// Apply Northstar offsets to sample structure
						*(void**)(sampleStructure + 0xE8) = const_cast<void*>(overridePtr);
						*(unsigned int*)(sampleStructure + 0xF0) = overrideLen;
						
						if (wav_debug.GetBool())
							Msg(eDLL_T::AUDIO, "[NORTHSTAR] Successfully applied override - Buffer: %p, Len: %u\n",
								overridePtr, overrideLen);
					}
					__except(EXCEPTION_EXECUTE_HANDLER)
					{
						if (wav_debug.GetBool())
							Msg(eDLL_T::AUDIO, "[NORTHSTAR] Failed to write to sample structure %p\n", 
								(void*)sampleStructure);
					}
				}
				else
				{
					if (wav_debug.GetBool())
						Msg(eDLL_T::AUDIO, "[NORTHSTAR] No valid sample found in event structure\n");
				}
			}
			__except(EXCEPTION_EXECUTE_HANDLER)
			{
				if (wav_debug.GetBool())
					Msg(eDLL_T::AUDIO, "[NORTHSTAR] Failed to access event structure for override\n");
			}
		}
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		if (wav_debug.GetBool())
			Msg(eDLL_T::AUDIO, "[NORTHSTAR] Exception during event name extraction\n");
	}
	
	return result;
}

static __int64 __fastcall MilesEventBuild_Hook(float* a1, __int64 a2, __int64 a3)
{
	return v_MilesEventBuild(a1, a2, a3);
}
*/

static __int64 sub_1409A7570_Hook(__int64 a1, char* a2)
{
	Msg(eDLL_T::AUDIO, "sub_1409A7570_Hook: %s\n", a2);
	
	return v_sub_1409A7570(a1, a2);
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
	DetourSetup(&v_CSOM_AddEventToQueue, &CSOM_AddEventToQueue, bAttach);
	DetourSetup(&v_ProcessClientAnimEvent, &ProcessClientAnimEvent, bAttach);
	DetourSetup(&v_StopSoundOnEntityForLocalPlayer, &StopSoundOnEntityForLocalPlayer, bAttach);
	DetourSetup(&v_EmitSoundOnEntityForLocalPlayer, &EmitSoundOnEntityForLocalPlayer, bAttach);
	DetourSetup(&v_Charge_EmitSoundOnEntityForLocalPlayer, &Charge_EmitSoundOnEntityForLocalPlayer, bAttach);
	DetourSetup(&v_EmitSoundOnEntity, &EmitSoundOnEntity, bAttach);
	DetourSetup(&v_EmitSoundOnEntityImpl, &EmitSoundOnEntityImpl_Hook, bAttach);
	DetourSetup(&v_sub_1409A7570, &sub_1409A7570_Hook, bAttach);

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
	}
}
