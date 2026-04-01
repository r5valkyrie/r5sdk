#include "core/stdafx.h"
#include "scriptremotefunctions_shared.h"

static ScriptRemoteFuncDesc_t s_allowlist[SCRIPT_REMOTE_SERVER_MAX_FUNCTIONS];
static int s_nAllowlistCount = 0;
static bool s_bRegistrationLocked = false;

bool ScriptRemoteServer_RegisterFunction(const char* pszName, int nParamCount,
	const ScriptRemoteParamDesc_t* pParams)
{
	if (s_bRegistrationLocked)
	{
		Warning(eDLL_T::SERVER, "ScriptRemoteServer: registration locked, rejecting '%s'\n", pszName);
		return false;
	}

	if (!pszName || !*pszName)
	{
		Warning(eDLL_T::SERVER, "ScriptRemoteServer: empty function name\n");
		return false;
	}

	if (nParamCount < 0 || nParamCount > SCRIPT_REMOTE_SERVER_MAX_PARAMS)
	{
		Warning(eDLL_T::SERVER, "ScriptRemoteServer: '%s' has %d params (max %d)\n",
			pszName, nParamCount, SCRIPT_REMOTE_SERVER_MAX_PARAMS);
		return false;
	}

	if (s_nAllowlistCount >= SCRIPT_REMOTE_SERVER_MAX_FUNCTIONS)
	{
		Warning(eDLL_T::SERVER, "ScriptRemoteServer: allowlist full\n");
		return false;
	}

	for (int i = 0; i < s_nAllowlistCount; i++)
	{
		if (strcmp(s_allowlist[i].szName, pszName) == 0)
			return true;
	}

	ScriptRemoteFuncDesc_t& entry = s_allowlist[s_nAllowlistCount];
	V_strncpy(entry.szName, pszName, sizeof(entry.szName));
	entry.nIndex = static_cast<uint16_t>(s_nAllowlistCount);
	entry.nParamCount = nParamCount;

	for (int i = 0; i < nParamCount; i++)
		entry.params[i] = pParams[i];

	s_nAllowlistCount++;
	DevMsg(eDLL_T::SERVER, "ScriptRemoteServer: registered '%s' (index=%d, params=%d)\n",
		pszName, entry.nIndex, nParamCount);
	return true;
}

const ScriptRemoteFuncDesc_t* ScriptRemoteServer_FindFunction(const char* pszName)
{
	for (int i = 0; i < s_nAllowlistCount; i++)
	{
		if (strcmp(s_allowlist[i].szName, pszName) == 0)
			return &s_allowlist[i];
	}
	return nullptr;
}

const ScriptRemoteFuncDesc_t* ScriptRemoteServer_GetFunctionByIndex(uint16_t nIndex)
{
	if (nIndex >= static_cast<uint16_t>(s_nAllowlistCount))
		return nullptr;
	return &s_allowlist[nIndex];
}

int ScriptRemoteServer_GetFunctionCount()
{
	return s_nAllowlistCount;
}

uint32_t ScriptRemoteServer_CalcChecksum()
{
	constexpr uint32_t FNV_PRIME = 0x01000193;
	constexpr uint32_t FNV_OFFSET = 0x811c9dc5;

	uint32_t hash = FNV_OFFSET;

	for (int i = 0; i < s_nAllowlistCount; i++)
	{
		const ScriptRemoteFuncDesc_t& entry = s_allowlist[i];

		for (const char* p = entry.szName; *p; p++)
		{
			hash ^= static_cast<uint32_t>(*p);
			hash *= FNV_PRIME;
		}

		hash ^= static_cast<uint32_t>(entry.nParamCount);
		hash *= FNV_PRIME;

		for (int j = 0; j < entry.nParamCount; j++)
		{
			const uint8_t* pBytes = reinterpret_cast<const uint8_t*>(&entry.params[j]);
			for (size_t k = 0; k < sizeof(entry.params[j]); k++)
			{
				hash ^= pBytes[k];
				hash *= FNV_PRIME;
			}
		}
	}

	return hash;
}

void ScriptRemoteServer_LockRegistrations()
{
	s_bRegistrationLocked = true;
	DevMsg(eDLL_T::SERVER, "ScriptRemoteServer: locked (%d functions)\n", s_nAllowlistCount);
}

void ScriptRemoteServer_ClearRegistrations()
{
	s_nAllowlistCount = 0;
	s_bRegistrationLocked = false;
	memset(s_allowlist, 0, sizeof(s_allowlist));
}
