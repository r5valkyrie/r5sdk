//=============================================================================//
//
// Purpose: ScriptNetData - Networked Variable Change Callbacks for CLIENT/UI
// Engine only registers these for CLIENT VM; SDK provides them for UI VM too
//
//=============================================================================//

#include "core/stdafx.h"
#include "tier1/utlmap.h"
#include "tier1/utlvector.h"
#include "vscript/vscript.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "game/shared/vscript_gamedll_defs.h"
#include "vscript/languages/squirrel_re/vsquirrel.h"
#include "game/shared/vscript_shared.h"
#include "vscript_client.h"
#include "scriptnetdata_client.h"

//------------------------------------------------------------------------------
// Callback Storage
//------------------------------------------------------------------------------
struct NetVarCallback_t
{
	SQObject callback;
	ScriptNetVarType_e type;

	NetVarCallback_t() : type(SNVT_BOOL)
	{
		callback._type = OT_NULL;
		callback._unVal.pUserPointer = nullptr;
	}
};

// Separate storage per VM
static CUtlMap<uint32_t, CUtlVector<NetVarCallback_t>*> s_ClientCallbacks;
static CUtlMap<uint32_t, CUtlVector<NetVarCallback_t>*> s_UICallbacks;
static bool s_bInitialized = false;

//------------------------------------------------------------------------------
// Helpers
//------------------------------------------------------------------------------
static CUtlMap<uint32_t, CUtlVector<NetVarCallback_t>*>* GetCallbackMap(HSQUIRRELVM v)
{
	if (g_pClientScript && g_pClientScript->GetVM() == v)
		return &s_ClientCallbacks;
	if (g_pUIScript && g_pUIScript->GetVM() == v)
		return &s_UICallbacks;
	return nullptr;
}

static uint32_t HashVarName(const char* name)
{
	uint32_t hash = 2166136261u;
	while (*name)
	{
		hash ^= (uint8_t)*name++;
		hash *= 16777619u;
	}
	return hash;
}

static void EnsureInitialized()
{
	if (!s_bInitialized)
	{
		s_ClientCallbacks.SetLessFunc(DefLessFunc(uint32_t));
		s_UICallbacks.SetLessFunc(DefLessFunc(uint32_t));
		s_bInitialized = true;
	}
}

//------------------------------------------------------------------------------
// Internal: Register a callback
//------------------------------------------------------------------------------
static SQRESULT RegisterCallback_Internal(HSQUIRRELVM v, ScriptNetVarType_e type)
{
	EnsureInitialized();

	const SQChar* varName = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &varName)) || !varName)
	{
		v_SQVM_ScriptError("Expected string for variable name");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	SQObject callbackObj;
	if (SQ_FAILED(sq_getstackobj(v, 3, &callbackObj)))
	{
		v_SQVM_ScriptError("Failed to get callback object");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	if (!sq_isclosure(callbackObj) && !sq_isnativeclosure(callbackObj))
	{
		v_SQVM_ScriptError("Expected function for callback");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	CUtlMap<uint32_t, CUtlVector<NetVarCallback_t>*>* pCallbacks = GetCallbackMap(v);
	if (!pCallbacks)
	{
		v_SQVM_ScriptError("Unknown VM context");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	uint32_t hash = HashVarName(varName);

	unsigned short idx = pCallbacks->Find(hash);
	if (idx == pCallbacks->InvalidIndex())
	{
		CUtlVector<NetVarCallback_t>* pNewList = new CUtlVector<NetVarCallback_t>();
		idx = pCallbacks->Insert(hash, pNewList);
	}

	NetVarCallback_t cb;
	cb.type = type;
	cb.callback = callbackObj;

	sq_addref(v, &cb.callback);
	pCallbacks->Element(idx)->AddToTail(cb);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//------------------------------------------------------------------------------
// Native Functions - Registration (UI VM only, CLIENT has engine natives)
//------------------------------------------------------------------------------
static SQRESULT UIScript_RegisterNetworkedVariableChangeCallback_bool(HSQUIRRELVM v)
{
	return RegisterCallback_Internal(v, SNVT_BOOL);
}

static SQRESULT UIScript_RegisterNetworkedVariableChangeCallback_int(HSQUIRRELVM v)
{
	return RegisterCallback_Internal(v, SNVT_INT);
}

static SQRESULT UIScript_RegisterNetworkedVariableChangeCallback_float(HSQUIRRELVM v)
{
	return RegisterCallback_Internal(v, SNVT_FLOAT_RANGE);
}

static SQRESULT UIScript_RegisterNetworkedVariableChangeCallback_time(HSQUIRRELVM v)
{
	return RegisterCallback_Internal(v, SNVT_TIME);
}

static SQRESULT UIScript_RegisterNetworkedVariableChangeCallback_ent(HSQUIRRELVM v)
{
	return RegisterCallback_Internal(v, SNVT_ENTITY);
}

//------------------------------------------------------------------------------
// Trigger Callbacks
//------------------------------------------------------------------------------
static SQRESULT TriggerNetVarCallbacks_Internal(HSQUIRRELVM v)
{
	EnsureInitialized();

	const SQChar* varName = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &varName)) || !varName)
	{
		v_SQVM_ScriptError("Expected string for variable name");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	CUtlMap<uint32_t, CUtlVector<NetVarCallback_t>*>* pCallbacks = GetCallbackMap(v);
	if (!pCallbacks)
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

	uint32_t hash = HashVarName(varName);
	unsigned short idx = pCallbacks->Find(hash);
	if (idx == pCallbacks->InvalidIndex())
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

	SQObject entityObj, oldValueObj, newValueObj, changedObj;
	sq_getstackobj(v, 3, &entityObj);
	sq_getstackobj(v, 4, &oldValueObj);
	sq_getstackobj(v, 5, &newValueObj);
	sq_getstackobj(v, 6, &changedObj);

	CUtlVector<NetVarCallback_t>* pVec = pCallbacks->Element(idx);
	for (int i = 0; i < pVec->Count(); i++)
	{
		NetVarCallback_t& cb = (*pVec)[i];

		sq_pushobject(v, cb.callback);
		sq_pushroottable(v);
		sq_pushobject(v, entityObj);    // entity
		sq_pushobject(v, oldValueObj);  // oldValue
		sq_pushobject(v, newValueObj);  // newValue
		sq_pushobject(v, changedObj);   // actuallyChanged

		if (SQ_FAILED(sq_call(v, 5, SQFalse, SQTrue)))
		{
			sq_pop(v, 1);
			continue;
		}
		sq_pop(v, 1);
	}

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

// Separate entry points for CLIENT and UI macros
static SQRESULT ClientScript_TriggerNetVarCallbacks(HSQUIRRELVM v)
{
	return TriggerNetVarCallbacks_Internal(v);
}

static SQRESULT UIScript_TriggerNetVarCallbacks(HSQUIRRELVM v)
{
	return TriggerNetVarCallbacks_Internal(v);
}

//------------------------------------------------------------------------------
// Cleanup
//------------------------------------------------------------------------------
void ScriptNetData_OnVMDestroyed(CSquirrelVM* vm)
{
	if (!s_bInitialized || !vm)
		return;

	HSQUIRRELVM v = vm->GetVM();
	if (!v)
		return;

	CUtlMap<uint32_t, CUtlVector<NetVarCallback_t>*>* pCallbacks = nullptr;

	if (g_pClientScript && g_pClientScript == vm)
		pCallbacks = &s_ClientCallbacks;
	else if (g_pUIScript && g_pUIScript == vm)
		pCallbacks = &s_UICallbacks;

	if (pCallbacks)
	{
		for (unsigned short i = pCallbacks->FirstInorder();
			i != pCallbacks->InvalidIndex(); i = pCallbacks->NextInorder(i))
		{
			CUtlVector<NetVarCallback_t>* pVec = pCallbacks->Element(i);
			for (int j = 0; j < pVec->Count(); j++)
				sq_release(v, &(*pVec)[j].callback);
			delete pVec;
		}
		pCallbacks->RemoveAll();
	}
}

//------------------------------------------------------------------------------
// UI Stubs - GetPlayerNet* (return defaults, UI has no player net data)
//------------------------------------------------------------------------------
static SQRESULT UIScript_GetPlayerNetBool(HSQUIRRELVM v)
{
	sq_pushbool(v, SQFalse);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT UIScript_GetPlayerNetInt(HSQUIRRELVM v)
{
	sq_pushinteger(v, 0);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT UIScript_GetPlayerNetFloat(HSQUIRRELVM v)
{
	sq_pushfloat(v, 0.0f);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT UIScript_GetPlayerNetTime(HSQUIRRELVM v)
{
	sq_pushfloat(v, 0.0f);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT UIScript_GetPlayerNetEnt(HSQUIRRELVM v)
{
	sq_pushnull(v);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//------------------------------------------------------------------------------
// Registration Functions
//------------------------------------------------------------------------------
void ScriptNetData_RegisterClientFunctions(CSquirrelVM* vm)
{
	if (!vm)
		return;

	EnsureInitialized();

	vm->RegisterConstant("SNDC_GLOBAL_NON_REWIND", SNDC_GLOBAL_NON_REWIND);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(vm, TriggerNetVarCallbacks,
		"Triggers registered callbacks for a netvar",
		"void", "string varName, entity ent, var oldValue, var newValue, bool actuallyChanged", false);
}

void ScriptNetData_RegisterUIFunctions(CSquirrelVM* vm)
{
	if (!vm)
		return;

	EnsureInitialized();

	vm->RegisterConstant("SNDC_GLOBAL_NON_REWIND", SNDC_GLOBAL_NON_REWIND);

	DEFINE_UI_SCRIPTFUNC_NAMED(vm, RegisterNetworkedVariableChangeCallback_bool,
		"Registers a callback for bool netvar changes",
		"void", "string varName, void functionref(entity, bool, bool, bool) callback", false);
	DEFINE_UI_SCRIPTFUNC_NAMED(vm, RegisterNetworkedVariableChangeCallback_int,
		"Registers a callback for int netvar changes",
		"void", "string varName, void functionref(entity, int, int, bool) callback", false);
	DEFINE_UI_SCRIPTFUNC_NAMED(vm, RegisterNetworkedVariableChangeCallback_float,
		"Registers a callback for float netvar changes",
		"void", "string varName, void functionref(entity, float, float, bool) callback", false);
	DEFINE_UI_SCRIPTFUNC_NAMED(vm, RegisterNetworkedVariableChangeCallback_time,
		"Registers a callback for time netvar changes",
		"void", "string varName, void functionref(entity, float, float, bool) callback", false);
	DEFINE_UI_SCRIPTFUNC_NAMED(vm, RegisterNetworkedVariableChangeCallback_ent,
		"Registers a callback for entity netvar changes",
		"void", "string varName, void functionref(entity, entity, entity, bool) callback", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(vm, TriggerNetVarCallbacks,
		"Triggers registered callbacks for a netvar",
		"void", "string varName, entity ent, var oldValue, var newValue, bool actuallyChanged", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(vm, GetPlayerNetBool,
		"Gets a player bool net variable",
		"bool", "entity player, string varName", false);
	DEFINE_UI_SCRIPTFUNC_NAMED(vm, GetPlayerNetInt,
		"Gets a player int net variable",
		"int", "entity player, string varName", false);
	DEFINE_UI_SCRIPTFUNC_NAMED(vm, GetPlayerNetFloat,
		"Gets a player float net variable",
		"float", "entity player, string varName", false);
	DEFINE_UI_SCRIPTFUNC_NAMED(vm, GetPlayerNetTime,
		"Gets a player time net variable",
		"float", "entity player, string varName", false);
	DEFINE_UI_SCRIPTFUNC_NAMED(vm, GetPlayerNetEnt,
		"Gets a player entity net variable",
		"entity", "entity player, string varName", false);
}
