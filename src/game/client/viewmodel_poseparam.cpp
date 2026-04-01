//=============================================================================//
//
// Purpose: Script-controllable pose parameters for weapon viewmodels
//
//=============================================================================//

#include "core/stdafx.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "vscript/languages/squirrel_re/vsquirrel.h"
#include "game/shared/vscript_gamedll_defs.h"
#include "game/shared/weapon_heat.h"
#include "viewmodel_poseparam.h"

static constexpr __int64 VIEWMODEL_STUDIOHDR_OFFSET = 0x10E0;

static __int64 Hook_C_BaseViewModel_OnModelChanged(__int64 viewmodel)
{
	__int64 result = v_C_BaseViewModel_OnModelChanged(viewmodel);

	g_scriptPoseParamValues[0] = 0.0f;
	g_scriptPoseParamValues[1] = 0.0f;
	g_script0PoseParamIndex = -1;
	g_script1PoseParamIndex = -1;

	const __int64 studioHdr = *(__int64*)(viewmodel + VIEWMODEL_STUDIOHDR_OFFSET);

	if (studioHdr)
	{
		const __int64 idx0 = v_LookupPoseParameter(viewmodel, studioHdr, "script0");
		const __int64 idx1 = v_LookupPoseParameter(viewmodel, studioHdr, "script1");

		g_script0PoseParamIndex = static_cast<int>(idx0);
		g_script1PoseParamIndex = static_cast<int>(idx1);

		if (g_script0PoseParamIndex >= 0 || g_script1PoseParamIndex >= 0)
		{
			DevMsg(eDLL_T::CLIENT, "Viewmodel script pose params: script0=%d, script1=%d\n",
				g_script0PoseParamIndex, g_script1PoseParamIndex);
		}
	}

	return result;
}

static __int64 Hook_C_BaseViewModel_UpdatePoseParameters(__int64 viewmodel, __int64 player)
{
	const __int64 result = v_C_BaseViewModel_UpdatePoseParameters(viewmodel, player);

	WeaponHeat_UpdateAll();

	const __int64 studioHdr = *(__int64*)(viewmodel + VIEWMODEL_STUDIOHDR_OFFSET);

	if (studioHdr)
	{
		if (g_script0PoseParamIndex >= 0)
			v_SetPoseParameter(viewmodel, studioHdr, g_script0PoseParamIndex, g_scriptPoseParamValues[0]);

		if (g_script1PoseParamIndex >= 0)
			v_SetPoseParameter(viewmodel, studioHdr, g_script1PoseParamIndex, g_scriptPoseParamValues[1]);
	}

	return result;
}

static SQRESULT Script_SetScriptPoseParam0(HSQUIRRELVM v)
{
	SQFloat value;
	sq_getfloat(v, 2, &value);
	g_scriptPoseParamValues[0] = static_cast<float>(value);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_GetScriptPoseParam0(HSQUIRRELVM v)
{
	sq_pushfloat(v, g_scriptPoseParamValues[0]);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_SetScriptPoseParam1(HSQUIRRELVM v)
{
	SQFloat value;
	sq_getfloat(v, 2, &value);
	g_scriptPoseParamValues[1] = static_cast<float>(value);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_GetScriptPoseParam1(HSQUIRRELVM v)
{
	sq_pushfloat(v, g_scriptPoseParamValues[1]);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

void ViewmodelPoseParam_RegisterClientWeaponFuncs(ScriptClassDescriptor_t* weaponStruct)
{
	weaponStruct->AddFunction(
		"SetScriptPoseParam0",
		"Script_SetScriptPoseParam0",
		"Sets script pose parameter 0 on the weapon viewmodel",
		"void",
		"float value",
		false,
		Script_SetScriptPoseParam0);

	weaponStruct->AddFunction(
		"GetScriptPoseParam0",
		"Script_GetScriptPoseParam0",
		"Gets the current script pose parameter 0 value",
		"float",
		"",
		false,
		Script_GetScriptPoseParam0);

	weaponStruct->AddFunction(
		"SetScriptPoseParam1",
		"Script_SetScriptPoseParam1",
		"Sets script pose parameter 1 on the weapon viewmodel",
		"void",
		"float value",
		false,
		Script_SetScriptPoseParam1);

	weaponStruct->AddFunction(
		"GetScriptPoseParam1",
		"Script_GetScriptPoseParam1",
		"Gets the current script pose parameter 1 value",
		"float",
		"",
		false,
		Script_GetScriptPoseParam1);
}

void VViewmodelPoseParam::Detour(const bool bAttach) const
{
	DetourSetup(&v_C_BaseViewModel_OnModelChanged, &Hook_C_BaseViewModel_OnModelChanged, bAttach);
	DetourSetup(&v_C_BaseViewModel_UpdatePoseParameters, &Hook_C_BaseViewModel_UpdatePoseParameters, bAttach);
}
