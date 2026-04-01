//=============================================================================//
// Purpose: Client-side natives for Remote_RegisterServerFunction system
//=============================================================================//

#include "core/stdafx.h"
#include "tier1/bitbuf.h"
#include "common/netmessages.h"
#include "engine/cmd.h"
#include "engine/client/clientstate.h"
#include "vscript/vscript.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "game/shared/vscript_gamedll_defs.h"
#include "vscript/languages/squirrel_re/vsquirrel.h"
#include "game/shared/scriptremotefunctions_shared.h"
#include "vscript_client.h"
#include "game/server/vscript_server.h"
#include "vscript_remotefunctions.h"

//-----------------------------------------------------------------------------
// Client-side registration state machine
//-----------------------------------------------------------------------------

enum class RemoteFuncClientState_e : int
{
	INACTIVE = 0,     // Not initialized
	REGISTERING = 1,  // Between BeginRegistering / EndRegistering
	ACTIVE = 2,       // Registration complete, calls allowed
};

struct ClientRemoteFuncEntry_t
{
	char szName[128];
	uint16_t nIndex;  // Dictionary index for binary protocol
	int nParamCount;
	ScriptRemoteParamDesc_t params[SCRIPT_REMOTE_SERVER_MAX_PARAMS];
};

static RemoteFuncClientState_e s_eClientState = RemoteFuncClientState_e::INACTIVE;
static ClientRemoteFuncEntry_t s_clientFuncTable[SCRIPT_REMOTE_SERVER_MAX_FUNCTIONS];
static int s_nClientFuncCount = 0;

//-----------------------------------------------------------------------------
// Purpose: clear all client-side registrations
// Called on disconnect / map change
//-----------------------------------------------------------------------------
void Script_ClearRemoteFunctionRegistrations()
{
	s_eClientState = RemoteFuncClientState_e::INACTIVE;
	s_nClientFuncCount = 0;
	memset(s_clientFuncTable, 0, sizeof(s_clientFuncTable));
}

//-----------------------------------------------------------------------------
// Purpose: find a client-side registered function by name
//-----------------------------------------------------------------------------
static const ClientRemoteFuncEntry_t* FindClientRemoteFunc(const char* pszName)
{
	for (int i = 0; i < s_nClientFuncCount; i++)
	{
		if (strcmp(s_clientFuncTable[i].szName, pszName) == 0)
			return &s_clientFuncTable[i];
	}
	return nullptr;
}

//=============================================================================//
// Script Natives
//=============================================================================//

//-----------------------------------------------------------------------------
// void Remote_BeginRegisteringServerFunctions()
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_Remote_BeginRegisteringServerFunctions(HSQUIRRELVM v)
{
	if (s_eClientState != RemoteFuncClientState_e::INACTIVE)
	{
		v_SQVM_RaiseError(v, "Remote_BeginRegisteringServerFunctions: already in state %d\n",
			static_cast<int>(s_eClientState));
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	s_eClientState = RemoteFuncClientState_e::REGISTERING;
	s_nClientFuncCount = 0;
	ScriptRemoteServer_ClearRegistrations();

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// void Remote_EndRegisteringServerFunctions()
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_Remote_EndRegisteringServerFunctions(HSQUIRRELVM v)
{
	if (s_eClientState != RemoteFuncClientState_e::REGISTERING)
	{
		v_SQVM_RaiseError(v, "Remote_EndRegisteringServerFunctions: not in registering state\n");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	s_eClientState = RemoteFuncClientState_e::ACTIVE;

	ScriptRemoteServer_LockRegistrations();

	DevMsg(eDLL_T::CLIENT, "ScriptRemoteClient: registered %d server functions\n", s_nClientFuncCount);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// void Remote_RegisterServerFunction(string functionName, ...)
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_Remote_RegisterServerFunction(HSQUIRRELVM v)
{
	if (s_eClientState == RemoteFuncClientState_e::INACTIVE)
	{
		s_eClientState = RemoteFuncClientState_e::REGISTERING;
		s_nClientFuncCount = 0;
		ScriptRemoteServer_ClearRegistrations();
	}

	if (s_nClientFuncCount >= SCRIPT_REMOTE_SERVER_MAX_FUNCTIONS)
	{
		v_SQVM_RaiseError(v, "Remote_RegisterServerFunction: too many functions registered\n");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	const SQChar* pszFuncName = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &pszFuncName)) || !pszFuncName || !*pszFuncName)
	{
		v_SQVM_RaiseError(v, "Remote_RegisterServerFunction: invalid function name\n");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	// Skip if already registered
	for (int i = 0; i < s_nClientFuncCount; i++)
	{
		if (strcmp(s_clientFuncTable[i].szName, pszFuncName) == 0)
		{
			SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
		}
	}

	ClientRemoteFuncEntry_t entry;
	memset(&entry, 0, sizeof(entry));
	V_strncpy(entry.szName, pszFuncName, sizeof(entry.szName));
	entry.nParamCount = 0;

	const SQInteger nTop = sq_gettop(v);
	SQInteger idx = 3; // Start after function name

	while (idx <= nTop)
	{
		if (entry.nParamCount >= SCRIPT_REMOTE_SERVER_MAX_PARAMS)
		{
			v_SQVM_RaiseError(v, "Remote_RegisterServerFunction: '%s' too many params\n", pszFuncName);
			SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
		}

		const SQChar* pszType = nullptr;
		if (SQ_FAILED(sq_getstring(v, idx, &pszType)))
		{
			v_SQVM_RaiseError(v, "Remote_RegisterServerFunction: '%s' expected type string at arg %d\n",
				pszFuncName, static_cast<int>(idx));
			SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
		}

		ScriptRemoteParamDesc_t& param = entry.params[entry.nParamCount];
		memset(&param, 0, sizeof(param));

		if (strcmp(pszType, "int") == 0)
		{
			// "int", min, max[, precision] — need 2+ more args
			param.type = ScriptRemoteParamType_e::SRP_INT;
			if (idx + 2 > nTop)
			{
				v_SQVM_RaiseError(v, "Remote_RegisterServerFunction: '%s' int param missing min/max\n",
					pszFuncName);
				SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
			}

			SQInteger intMin = 0, intMax = 0;
			sq_getinteger(v, idx + 1, &intMin);
			sq_getinteger(v, idx + 2, &intMax);

			param.intMin = static_cast<int>(intMin);
			param.intMax = static_cast<int>(intMax);
			idx += 3;
		}
		else if (strcmp(pszType, "float") == 0)
		{
			// "float", min, max, bits — need 3 more args
			param.type = ScriptRemoteParamType_e::SRP_FLOAT;
			if (idx + 3 > nTop)
			{
				v_SQVM_RaiseError(v, "Remote_RegisterServerFunction: '%s' float param needs min, max, bits\n",
					pszFuncName);
				SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
			}

			SQFloat fMin = 0.0f, fMax = 0.0f;
			sq_getfloat(v, idx + 1, &fMin);
			sq_getfloat(v, idx + 2, &fMax);

			SQInteger bits = 32;
			sq_getinteger(v, idx + 3, &bits);

			if (bits < 1 || bits > 32)
			{
				v_SQVM_RaiseError(v, "Remote_RegisterServerFunction: '%s' float bit count must be 1-32\n",
					pszFuncName);
				SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
			}

			param.floatMin = fMin;
			param.floatMax = fMax;
			param.floatBits = static_cast<int>(bits);
			idx += 4;
		}
		else if (strcmp(pszType, "bool") == 0)
		{
			param.type = ScriptRemoteParamType_e::SRP_BOOL;
			idx += 1;
		}
		else if (strcmp(pszType, "vector") == 0)
		{
			// "vector", min, max, bits — same format as float
			param.type = ScriptRemoteParamType_e::SRP_VECTOR;
			if (idx + 3 > nTop)
			{
				v_SQVM_RaiseError(v, "Remote_RegisterServerFunction: '%s' vector param needs min, max, bits\n",
					pszFuncName);
				SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
			}

			SQFloat fMin = 0.0f, fMax = 0.0f;
			sq_getfloat(v, idx + 1, &fMin);
			sq_getfloat(v, idx + 2, &fMax);

			SQInteger bits = 32;
			sq_getinteger(v, idx + 3, &bits);

			if (bits < 1 || bits > 32)
			{
				v_SQVM_RaiseError(v, "Remote_RegisterServerFunction: '%s' vector bit count must be 1-32\n",
					pszFuncName);
				SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
			}

			param.floatMin = fMin;
			param.floatMax = fMax;
			param.floatBits = static_cast<int>(bits);
			idx += 4;
		}
		else if (strcmp(pszType, "entity") == 0)
		{
			param.type = ScriptRemoteParamType_e::SRP_ENTITY;
			idx += 1;
		}
		else if (strcmp(pszType, "typed_entity") == 0)
		{
			param.type = ScriptRemoteParamType_e::SRP_TYPED_ENTITY;
			idx += 2;
		}
		else if (strcmp(pszType, "itemflavor") == 0)
		{
			// "itemflavor", int [, int] — 2 or 3 args (single type or range)
			param.type = ScriptRemoteParamType_e::SRP_ITEMFLAVOR;
			if (idx + 1 > nTop)
			{
				v_SQVM_RaiseError(v, "Remote_RegisterServerFunction: '%s' itemflavor needs at least 1 int\n",
					pszFuncName);
				SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
			}

			SQInteger itemType1 = 0;
			sq_getinteger(v, idx + 1, &itemType1);
			param.intMin = static_cast<int>(itemType1);
			idx += 2;

			// Optional second int (range max)
			if (idx <= nTop)
			{
				SQInteger testVal;
				if (SQ_SUCCEEDED(sq_getinteger(v, idx, &testVal)))
				{
					param.intMax = static_cast<int>(testVal);
					idx++;
				}
			}
		}
		else
		{
			v_SQVM_RaiseError(v, "Remote_RegisterServerFunction: '%s' unknown type '%s'\n",
				pszFuncName, pszType);
			SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
		}

		entry.nParamCount++;
	}

	entry.nIndex = static_cast<uint16_t>(s_nClientFuncCount);
	s_clientFuncTable[s_nClientFuncCount] = entry;
	s_nClientFuncCount++;

	ScriptRemoteServer_RegisterFunction(entry.szName, entry.nParamCount, entry.params);

	DevMsg(eDLL_T::CLIENT, "ScriptRemoteClient: registered '%s' (index=%d, params=%d)\n",
		pszFuncName, entry.nIndex, entry.nParamCount);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// void Remote_ServerCallFunction(string functionName, ...)
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_Remote_ServerCallFunction(HSQUIRRELVM v)
{
	if (s_eClientState == RemoteFuncClientState_e::INACTIVE)
	{
		v_SQVM_RaiseError(v, "Remote_ServerCallFunction: remote functions not initialized\n");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	const SQChar* pszFuncName = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &pszFuncName)) || !pszFuncName || !*pszFuncName)
	{
		v_SQVM_RaiseError(v, "Remote_ServerCallFunction: invalid function name\n");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	const ClientRemoteFuncEntry_t* pEntry = FindClientRemoteFunc(pszFuncName);
	if (!pEntry)
	{
		v_SQVM_RaiseError(v, "Remote_ServerCallFunction: '%s' not registered\n", pszFuncName);
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	const SQInteger nTop = sq_gettop(v);
	const int nProvidedArgs = static_cast<int>(nTop) - 2;
	if (nProvidedArgs != pEntry->nParamCount)
	{
		v_SQVM_RaiseError(v, "Remote_ServerCallFunction: '%s' expected %d args, got %d\n",
			pszFuncName, pEntry->nParamCount, nProvidedArgs);
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	CNetChan* pNetChan = g_pClientState ? g_pClientState->m_NetChannel : nullptr;
	if (!pNetChan)
	{
		v_SQVM_RaiseError(v, "Remote_ServerCallFunction: no network connection\n");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	NET_ScriptMessage msg;
	msg.InitWrite();
	msg.m_bIsTyped = true;

	msg.m_DataOut.WriteUBitLong(pEntry->nIndex, SCRIPT_REMOTE_FUNC_INDEX_BITS);
	msg.m_DataOut.WriteLong(0); // reserved

	for (int i = 0; i < pEntry->nParamCount; i++)
	{
		const SQInteger sqIdx = i + 3;
		const ScriptRemoteParamDesc_t& paramDesc = pEntry->params[i];

		switch (paramDesc.type)
		{
		case ScriptRemoteParamType_e::SRP_BOOL:
		{
			SQBool val = false;
			sq_getbool(v, sqIdx, &val);
			msg.m_DataOut.WriteOneBit(val ? 1 : 0);
			break;
		}
		case ScriptRemoteParamType_e::SRP_INT:
		{
			SQInteger val = 0;
			sq_getinteger(v, sqIdx, &val);

			int intVal = static_cast<int>(val);
			if (intVal < paramDesc.intMin || intVal > paramDesc.intMax)
			{
				v_SQVM_RaiseError(v, "Remote_ServerCallFunction: '%s' arg %d value %d out of range [%d, %d)\n",
					pszFuncName, i, intVal, paramDesc.intMin, paramDesc.intMax);
				SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
			}

			// Variable-width bit encoding — compute bits needed for range
			unsigned int range = static_cast<unsigned int>(paramDesc.intMax - paramDesc.intMin);
			int bits = 0;
			for (unsigned int r = range; r; r >>= 1)
				bits++;

			msg.m_DataOut.WriteUBitLong(intVal - paramDesc.intMin, bits);
			break;
		}
		case ScriptRemoteParamType_e::SRP_FLOAT:
		{
			SQFloat val = 0.0f;
			sq_getfloat(v, sqIdx, &val);

			if (paramDesc.floatBits >= 32)
			{
				msg.m_DataOut.WriteFloat(val);
			}
			else
			{
				if (val < paramDesc.floatMin || val > paramDesc.floatMax)
				{
					v_SQVM_RaiseError(v, "Remote_ServerCallFunction: '%s' arg %d value %f out of range [%f, %f]\n",
						pszFuncName, i, val, paramDesc.floatMin, paramDesc.floatMax);
					SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
				}

				float range = paramDesc.floatMax - paramDesc.floatMin;
				unsigned int maxVal = (1u << paramDesc.floatBits) - 1;
				unsigned int encoded = static_cast<unsigned int>(
					((val - paramDesc.floatMin) / range) * maxVal + 0.5f);

				msg.m_DataOut.WriteUBitLong(encoded, paramDesc.floatBits);
			}
			break;
		}
		case ScriptRemoteParamType_e::SRP_VECTOR:
		{
			const SQVector3D* vec = nullptr;
			sq_getvector(v, sqIdx, &vec);

			if (!vec)
			{
				for (int c = 0; c < 3; c++)
					msg.m_DataOut.WriteFloat(0.0f);
				break;
			}

			float components[3] = { vec->x, vec->y, vec->z };

			if (paramDesc.floatBits >= 32)
			{
				for (int c = 0; c < 3; c++)
					msg.m_DataOut.WriteFloat(components[c]);
			}
			else
			{
				for (int c = 0; c < 3; c++)
				{
					if (components[c] < paramDesc.floatMin || components[c] > paramDesc.floatMax)
					{
						v_SQVM_RaiseError(v, "Remote_ServerCallFunction: '%s' arg %d vector component %d out of range\n",
							pszFuncName, i, c);
						SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
					}

					float range = paramDesc.floatMax - paramDesc.floatMin;
					unsigned int maxVal = (1u << paramDesc.floatBits) - 1;
					unsigned int encoded = static_cast<unsigned int>(
						((components[c] - paramDesc.floatMin) / range) * maxVal + 0.5f);

					msg.m_DataOut.WriteUBitLong(encoded, paramDesc.floatBits);
				}
			}
			break;
		}
		case ScriptRemoteParamType_e::SRP_ENTITY:
		case ScriptRemoteParamType_e::SRP_TYPED_ENTITY:
		{
			// Writes entity.m_RefEHandle.m_Index via WriteLong
			void* pEnt = nullptr;
			if (v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pEnt)) && pEnt)
			{
				// m_RefEHandle.m_Index is at entity + 0x8 (IHandleEntity vtable)
				// But in R5 the EHANDLE is accessed via GetRefEHandle
				// The entity index field for networking uses m_Index from CBaseHandle
				int ehandle = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEnt) + 8);
				msg.m_DataOut.WriteLong(ehandle);
			}
			else
			{
				msg.m_DataOut.WriteLong(0);
			}
			break;
		}
		case ScriptRemoteParamType_e::SRP_ITEMFLAVOR:
		{
			SQInteger val = 0;
			sq_getinteger(v, sqIdx, &val);
			msg.m_DataOut.WriteLong(static_cast<int>(val));
			break;
		}
		default:
		{
			v_SQVM_RaiseError(v, "Remote_ServerCallFunction: '%s' arg %d unknown type\n",
				pszFuncName, i);
			SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
		}
		}
	}

	if (msg.m_DataOut.IsOverflowed())
	{
		v_SQVM_RaiseError(v, "Remote_ServerCallFunction: message buffer overflow\n");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	pNetChan->SendNetMsg(msg, true, false);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//=============================================================================//
// Server-side aliases (DEFINE_SERVER_SCRIPTFUNC_NAMED expects ServerScript_ prefix)
//=============================================================================//

static SQRESULT ServerScript_Remote_BeginRegisteringServerFunctions(HSQUIRRELVM v)
{
	return ClientScript_Remote_BeginRegisteringServerFunctions(v);
}

static SQRESULT ServerScript_Remote_EndRegisteringServerFunctions(HSQUIRRELVM v)
{
	return ClientScript_Remote_EndRegisteringServerFunctions(v);
}

static SQRESULT ServerScript_Remote_RegisterServerFunction(HSQUIRRELVM v)
{
	return ClientScript_Remote_RegisterServerFunction(v);
}

static SQRESULT ServerScript_Remote_ServerCallFunction(HSQUIRRELVM v)
{
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT ClientScript_Remote_ServerCallFunctionAllowed(HSQUIRRELVM v)
{
	sq_pushbool(v, s_eClientState == RemoteFuncClientState_e::ACTIVE);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT ServerScript_Remote_ServerCallFunctionAllowed(HSQUIRRELVM v)
{
	sq_pushbool(v, true);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//=============================================================================//
// UI-side aliases (DEFINE_UI_SCRIPTFUNC_NAMED expects UIScript_ prefix)
//=============================================================================//

static SQRESULT UIScript_Remote_BeginRegisteringServerFunctions(HSQUIRRELVM v)
{
	return ClientScript_Remote_BeginRegisteringServerFunctions(v);
}

static SQRESULT UIScript_Remote_EndRegisteringServerFunctions(HSQUIRRELVM v)
{
	return ClientScript_Remote_EndRegisteringServerFunctions(v);
}

static SQRESULT UIScript_Remote_RegisterServerFunction(HSQUIRRELVM v)
{
	return ClientScript_Remote_RegisterServerFunction(v);
}

static SQRESULT UIScript_Remote_ServerCallFunction(HSQUIRRELVM v)
{
	return ClientScript_Remote_ServerCallFunction(v);
}

static SQRESULT UIScript_Remote_ServerCallFunctionAllowed(HSQUIRRELVM v)
{
	sq_pushbool(v, s_eClientState == RemoteFuncClientState_e::ACTIVE);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//=============================================================================//
// Registration
//=============================================================================//

//-----------------------------------------------------------------------------
// Purpose: register all remote function natives into the CLIENT VM
//-----------------------------------------------------------------------------
void Script_RegisterRemoteFunctionNatives(CSquirrelVM* s)
{
	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Remote_BeginRegisteringServerFunctions,
		"Begin remote server function registration phase",
		"void", "", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Remote_EndRegisteringServerFunctions,
		"End remote server function registration phase",
		"void", "", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Remote_RegisterServerFunction,
		"Register a function the client can invoke on the server. Args: functionName, [type, min, max, ...]",
		"void", "string functionName, ...", true);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Remote_ServerCallFunction,
		"Call a registered server function with arguments",
		"void", "string functionName, ...", true);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Remote_ServerCallFunctionAllowed,
		"Returns true if remote server function calls are allowed",
		"bool", "", false);
}

//-----------------------------------------------------------------------------
// Purpose: register all remote function natives into the SERVER VM
//-----------------------------------------------------------------------------
void Script_RegisterRemoteFunctionServerNatives(CSquirrelVM* s)
{
	DEFINE_SERVER_SCRIPTFUNC_NAMED(s, Remote_BeginRegisteringServerFunctions,
		"Begin remote server function registration phase",
		"void", "", false);

	DEFINE_SERVER_SCRIPTFUNC_NAMED(s, Remote_EndRegisteringServerFunctions,
		"End remote server function registration phase",
		"void", "", false);

	DEFINE_SERVER_SCRIPTFUNC_NAMED(s, Remote_RegisterServerFunction,
		"Register a function the client can invoke on the server. Args: functionName, [type, min, max, ...]",
		"void", "string functionName, ...", true);

	DEFINE_SERVER_SCRIPTFUNC_NAMED(s, Remote_ServerCallFunction,
		"Call a registered server function with arguments",
		"void", "string functionName, ...", true);

	DEFINE_SERVER_SCRIPTFUNC_NAMED(s, Remote_ServerCallFunctionAllowed,
		"Returns true if remote server function calls are allowed",
		"bool", "", false);
}

//-----------------------------------------------------------------------------
// Purpose: register all remote function natives into the UI VM
//-----------------------------------------------------------------------------
void Script_RegisterRemoteFunctionUINatives(CSquirrelVM* s)
{
	DEFINE_UI_SCRIPTFUNC_NAMED(s, Remote_BeginRegisteringServerFunctions,
		"Begin remote server function registration phase",
		"void", "", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Remote_EndRegisteringServerFunctions,
		"End remote server function registration phase",
		"void", "", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Remote_RegisterServerFunction,
		"Register a function the client can invoke on the server. Args: functionName, [type, min, max, ...]",
		"void", "string functionName, ...", true);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Remote_ServerCallFunction,
		"Call a registered server function with arguments",
		"void", "string functionName, ...", true);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Remote_ServerCallFunctionAllowed,
		"Returns true if remote server function calls are allowed",
		"bool", "", false);
}
