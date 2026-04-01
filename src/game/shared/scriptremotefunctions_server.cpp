#include "core/stdafx.h"
#include "tier1/bitbuf.h"
#include "common/netmessages.h"
#include "engine/client/client.h"
#include "vscript/languages/squirrel_re/vsquirrel.h"
#include "game/server/baseentity.h"
#include "game/server/util_server.h"
#include "scriptremotefunctions_server.h"

bool ScriptRemoteServer_ProcessMessage(CClient* pClient, NET_ScriptMessage* pMsg)
{
	if (!pClient || !pMsg)
		return false;

	bf_read& in = pMsg->m_DataIn;

	const uint32_t nFuncIndex = in.ReadUBitLong(SCRIPT_REMOTE_FUNC_INDEX_BITS);

	const ScriptRemoteFuncDesc_t* pFunc = ScriptRemoteServer_GetFunctionByIndex(static_cast<uint16_t>(nFuncIndex));
	if (!pFunc)
	{
		Warning(eDLL_T::SERVER, "ScriptRemoteServer: invalid function index %u (client #%d)\n",
			nFuncIndex, pClient->GetUserID());
		return false;
	}

	in.ReadLong(); // reserved

	ScriptVariant_t scriptArgs[SCRIPT_REMOTE_SERVER_MAX_PARAMS + 1];
	int nScriptArgCount = 0;

	CPlayer* pPlayer = UTIL_PlayerByIndex(pClient->GetHandle());
	if (!pPlayer)
	{
		Warning(eDLL_T::SERVER, "ScriptRemoteServer: no player entity for client #%d\n", pClient->GetUserID());
		return false;
	}

	const HSCRIPT hPlayerScript = pPlayer->GetScriptInstance();
	if (!hPlayerScript)
	{
		Warning(eDLL_T::SERVER, "ScriptRemoteServer: player has no script instance (client #%d)\n", pClient->GetUserID());
		return false;
	}

	scriptArgs[nScriptArgCount++] = hPlayerScript;

	for (int i = 0; i < pFunc->nParamCount; i++)
	{
		const ScriptRemoteParamDesc_t& paramDesc = pFunc->params[i];

		switch (paramDesc.type)
		{
		case ScriptRemoteParamType_e::SRP_BOOL:
		{
			scriptArgs[nScriptArgCount++] = (in.ReadOneBit() != 0);
			break;
		}
		case ScriptRemoteParamType_e::SRP_INT:
		{
			const int val = in.ReadLong();
			if (val < paramDesc.intMin || val > paramDesc.intMax)
			{
				Warning(eDLL_T::SERVER, "ScriptRemoteServer: '%s' param %d out of range (client #%d)\n",
					pFunc->szName, i, pClient->GetUserID());
				return false;
			}
			scriptArgs[nScriptArgCount++] = val;
			break;
		}
		case ScriptRemoteParamType_e::SRP_FLOAT:
		{
			const float val = in.ReadFloat();
			if (!isfinite(val) || val < paramDesc.floatMin || val > paramDesc.floatMax)
			{
				Warning(eDLL_T::SERVER, "ScriptRemoteServer: '%s' param %d invalid (client #%d)\n",
					pFunc->szName, i, pClient->GetUserID());
				return false;
			}
			scriptArgs[nScriptArgCount++] = val;
			break;
		}
		default:
			return false;
		}
	}

	if (in.IsOverflowed())
		return false;

	if (!g_pServerScript)
		return false;

	HSCRIPT hFunc = g_pServerScript->FindFunction(pFunc->szName, nullptr, nullptr);
	if (!hFunc)
	{
		Warning(eDLL_T::SERVER, "ScriptRemoteServer: '%s' not found in VM\n", pFunc->szName);
		return false;
	}

	g_pServerScript->ExecuteFunction(hFunc, scriptArgs, nScriptArgCount, nullptr, nullptr);
	return true;
}

