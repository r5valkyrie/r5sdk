//=============================================================================//
// Purpose: Extended argument buffer for remote client function registration
//=============================================================================//

#include "core/stdafx.h"
#include "scriptremotefunctions.h"
#include <mutex>

static char s_ExtendedArgBuffer[SCRIPT_REMOTE_ARG_BUFFER_SIZE];
static int s_nExtendedArgBufferUsed = 0;
static std::mutex s_Mutex;

constexpr int ORIG_MAX_ENTRIES = 256;
constexpr int ORIG_ARG_BUFFER_SIZE = 2048;
constexpr int ORIG_ENTRY_SIZE = 24;
constexpr int ORIG_ENTRY_ARRAY_SIZE = ORIG_MAX_ENTRIES * ORIG_ENTRY_SIZE;
constexpr int ORIG_BUFFER_OFFSET = ORIG_ENTRY_ARRAY_SIZE;
constexpr int ORIG_COUNT_OFFSET = ORIG_BUFFER_OFFSET + ORIG_ARG_BUFFER_SIZE;
constexpr int ORIG_USED_OFFSET = ORIG_COUNT_OFFSET + 4;

char ScriptRemote_AddEntry(__int64 a1, __int64 a2, char a3, char a4, char a5, void* Src)
{
	if (!a1)
	{
		Warning(eDLL_T::ENGINE, "ScriptRemote_AddEntry: NULL base pointer\n");
		return 0;
	}

	std::lock_guard<std::mutex> lock(s_Mutex);

	const int argDataSize = static_cast<unsigned char>(a5);

	int* pEntryCount = reinterpret_cast<int*>(a1 + ORIG_COUNT_OFFSET);
	int* pBufferUsed = reinterpret_cast<int*>(a1 + ORIG_USED_OFFSET);
	char* pArgBuffer = reinterpret_cast<char*>(a1 + ORIG_BUFFER_OFFSET);

	const int entryIndex = *pEntryCount;
	const int bufferUsed = *pBufferUsed;

	if (entryIndex < 0 || entryIndex >= ORIG_MAX_ENTRIES)
	{
		Warning(eDLL_T::ENGINE, "ScriptRemote_AddEntry: entry index %d out of range [0, %d)\n", entryIndex, ORIG_MAX_ENTRIES);
		return 0;
	}

	if (bufferUsed < 0 || bufferUsed > ORIG_ARG_BUFFER_SIZE)
	{
		Warning(eDLL_T::ENGINE, "ScriptRemote_AddEntry: bufferUsed %d out of range [0, %d]\n", bufferUsed, ORIG_ARG_BUFFER_SIZE);
		return 0;
	}

	if (s_nExtendedArgBufferUsed < 0 || s_nExtendedArgBufferUsed > SCRIPT_REMOTE_ARG_BUFFER_SIZE)
	{
		Warning(eDLL_T::ENGINE, "ScriptRemote_AddEntry: extended buffer corrupted (%d), resetting\n", s_nExtendedArgBufferUsed);
		s_nExtendedArgBufferUsed = 0;
		return 0;
	}

	char* argDestPtr = nullptr;
	const int origRemaining = ORIG_ARG_BUFFER_SIZE - bufferUsed;

	if (argDataSize <= origRemaining)
	{
		argDestPtr = pArgBuffer + bufferUsed;
		*pBufferUsed = bufferUsed + argDataSize;
	}
	else
	{
		const int extRemaining = SCRIPT_REMOTE_ARG_BUFFER_SIZE - s_nExtendedArgBufferUsed;
		if (argDataSize > extRemaining)
		{
			Warning(eDLL_T::ENGINE, "ScriptRemote_AddEntry: BOTH buffers full! orig=%d/%d, ext=%d/%d, need=%d\n",
				bufferUsed, ORIG_ARG_BUFFER_SIZE, s_nExtendedArgBufferUsed, SCRIPT_REMOTE_ARG_BUFFER_SIZE, argDataSize);
			return 0;
		}

		DevMsg(eDLL_T::ENGINE, "ScriptRemote_AddEntry: using extended buffer (orig full: %d/%d, ext: %d/%d, need: %d)\n",
			bufferUsed, ORIG_ARG_BUFFER_SIZE, s_nExtendedArgBufferUsed, SCRIPT_REMOTE_ARG_BUFFER_SIZE, argDataSize);
		argDestPtr = s_ExtendedArgBuffer + s_nExtendedArgBufferUsed;
		s_nExtendedArgBufferUsed += argDataSize;
	}

	char* entry = reinterpret_cast<char*>(a1) + (entryIndex * ORIG_ENTRY_SIZE);
	(*pEntryCount)++;

	*reinterpret_cast<__int64*>(entry) = a2;
	entry[0x10] = a3;
	entry[0x11] = a4;
	entry[0x12] = a5;
	*reinterpret_cast<char**>(entry + 8) = argDestPtr;

	if (argDataSize > 0 && Src && argDestPtr)
		memmove(argDestPtr, Src, argDataSize);

	return 1;
}

__int64 ScriptRemote_RegisterName(__int64 a1, unsigned char* a2)
{
	if (!v_ScriptRemote_RegisterName)
		return 0;

	return v_ScriptRemote_RegisterName(a1, a2);
}

void VScriptRemoteFunctions::Detour(const bool bAttach) const
{
	if (!v_ScriptRemote_AddEntry)
	{
		Warning(eDLL_T::ENGINE, "ScriptRemoteFunctions: pattern for AddEntry NOT FOUND - hook disabled!\n");
		return;
	}

	DevMsg(eDLL_T::ENGINE, "ScriptRemoteFunctions: %s AddEntry hook (orig=0x%p, hook=0x%p)\n",
		bAttach ? "attaching" : "detaching", v_ScriptRemote_AddEntry, &ScriptRemote_AddEntry);

	DetourSetup(&v_ScriptRemote_AddEntry, &ScriptRemote_AddEntry, bAttach);

	if (v_ScriptRemote_RegisterName)
		DetourSetup(&v_ScriptRemote_RegisterName, &ScriptRemote_RegisterName, bAttach);

	if (bAttach)
	{
		std::lock_guard<std::mutex> lock(s_Mutex);
		memset(s_ExtendedArgBuffer, 0, SCRIPT_REMOTE_ARG_BUFFER_SIZE);
		s_nExtendedArgBufferUsed = 0;
		DevMsg(eDLL_T::ENGINE, "ScriptRemoteFunctions: extended buffer ready (%d bytes)\n", SCRIPT_REMOTE_ARG_BUFFER_SIZE);
	}
}
