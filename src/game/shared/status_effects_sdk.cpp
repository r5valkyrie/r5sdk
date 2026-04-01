//=============================================================================//
//
// Purpose: StatusEffect_GetTotalSeverity - SDK native implementation
//          Provides SUM semantics for status effect severity accumulation.
//
//=============================================================================//

#include "core/stdafx.h"
#include <cmath>
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "vscript/languages/squirrel_re/include/squirrel.h"
#include "game/shared/vscript_gamedll_defs.h"
#include "vscript/languages/squirrel_re/vsquirrel.h"
#include "game/shared/status_effect.h"
#include "status_effects_sdk.h"
#include "public/globalvars_base.h"

extern CGlobalVarsBase* gpGlobals;

//------------------------------------------------------------------------------
// Engine entity extraction function
//------------------------------------------------------------------------------
typedef void* (__fastcall *EntityExtractFn_t)(HSQUIRRELVM vm, SQObject* sqobj, void* typeDescAddr);
static EntityExtractFn_t s_fnEntityExtract = nullptr;

//------------------------------------------------------------------------------
// Entity type identification via vtable address comparison
//------------------------------------------------------------------------------
struct StatusEffectEntityInfo
{
	int timedOffset;
	int timedCount;
	int endlessOffset;
	int endlessCount;
};

struct StatusEffectContextInfo
{
	uintptr_t playerVtableRVA;
	uintptr_t titanSoulVtableRVA;
	uintptr_t npcVtableRVA;
	StatusEffectEntityInfo player;
	StatusEffectEntityInfo titanSoul;
	StatusEffectEntityInfo npc;
};

static const StatusEffectContextInfo s_serverInfo = {
	0x15391E0,   // CPlayer
	0x15B0140,   // CTitanSoul
	0x15983F8,   // CAI_BaseNPC
	{ 7456, 10, 7696, 10 },    // Player
	{ 6464, 3,  6536, 6 },     // TitanSoul
	{ 2576, 10, 2816, 10 },    // NPC
};

static const StatusEffectContextInfo s_clientInfo = {
	0x13E18B8,   // C_Player
	0x13E8AC8,   // C_TitanSoul
	0x13E6C98,   // C_AI_BaseNPC
	{ 29456, 10, 29696, 10 },  // Player
	{ 25784, 3,  25856, 6 },   // TitanSoul
	{ 2928,  10, 3168,  10 },  // NPC
};

static constexpr int STATUS_EFFECT_TYPE_MAX = 256;
static uintptr_t s_moduleBase = 0;

//------------------------------------------------------------------------------
// StatusEffects_SDK_Init
//------------------------------------------------------------------------------
static void StatusEffects_SDK_Init()
{
	CMemory entityExtractMem = Module_FindPattern(g_GameDll,
		"48 85 D2 75 16 8B 05 ?? ?? ?? ?? 83 F8 01 7D 08 FF C0 89 05 ?? ?? ?? ?? 33 C0 C3 F7 02 00 80 40 00");

	if (entityExtractMem)
	{
		entityExtractMem.GetPtr(s_fnEntityExtract);
		Msg(eDLL_T::ENGINE, "[StatusEffects_SDK] Entity extract function found at %p\n", s_fnEntityExtract);
	}
	else
	{
		Warning(eDLL_T::ENGINE, "[StatusEffects_SDK] Entity extract function not found!\n");
	}

	s_moduleBase = g_GameDll.GetModuleBase();
}

//------------------------------------------------------------------------------
// GetSeverityForTimedItem - cosine-based ease-out curve
//------------------------------------------------------------------------------
static float GetSeverityForTimedItem(const StatusEffectTimedData* item, float curTime)
{
	const float pausedTimeRemaining = item->sePausedTimeRemaining;
	const float seTimeEnd = item->seTimeEnd;
	const bool isPaused = pausedTimeRemaining > 0.0f;

	float timeRemaining;
	if (isPaused)
		timeRemaining = pausedTimeRemaining;
	else
		timeRemaining = seTimeEnd - curTime;

	if (curTime >= seTimeEnd && !isPaused)
		return 0.0f;

	const float baseSeverity = static_cast<float>(
		static_cast<unsigned char>(item->seComboVars >> 7)) / 255.0f;

	const float easeOut = item->seEaseOut;

	if (timeRemaining > easeOut || easeOut <= 0.0f)
		return baseSeverity;

	const float angle = (timeRemaining / easeOut) * 3.14159265f + 1.57079633f;
	const float factor = (1.0f - cosf(angle)) * 0.5f;
	return factor * baseSeverity;
}

//------------------------------------------------------------------------------
// ExtractEntityFromStack
//------------------------------------------------------------------------------
static void* ExtractEntityFromStack(HSQUIRRELVM v, SQInteger idx)
{
	if (!s_fnEntityExtract)
		return nullptr;

	SQObject obj;
	if (SQ_FAILED(sq_getstackobj(v, idx, &obj)))
		return nullptr;

	return s_fnEntityExtract(v, &obj, nullptr);
}

//------------------------------------------------------------------------------
// GetEntityTypeInfo
//------------------------------------------------------------------------------
static const StatusEffectEntityInfo* GetEntityTypeInfo(
	void* entity, const StatusEffectContextInfo& ctx)
{
	const uintptr_t vtable = *reinterpret_cast<uintptr_t*>(entity);
	const uintptr_t vtableRVA = vtable - s_moduleBase;

	if (vtableRVA == ctx.playerVtableRVA)
		return &ctx.player;
	if (vtableRVA == ctx.titanSoulVtableRVA)
		return &ctx.titanSoul;
	if (vtableRVA == ctx.npcVtableRVA)
		return &ctx.npc;

	return nullptr;
}

//------------------------------------------------------------------------------
// GetStatusEffectTotalSeverity
// Iterates timed + endless arrays, SUMs severity for matching effectType.
//------------------------------------------------------------------------------
static float GetStatusEffectTotalSeverity(
	void* entity, int effectType, float curTime,
	const StatusEffectContextInfo& ctx)
{
	const StatusEffectEntityInfo* entInfo = GetEntityTypeInfo(entity, ctx);
	if (!entInfo)
		return 0.0f;

	float totalSeverity = 0.0f;

	const StatusEffectTimedData* timedArr =
		reinterpret_cast<const StatusEffectTimedData*>(
			reinterpret_cast<const char*>(entity) + entInfo->timedOffset);

	for (int i = 0; i < entInfo->timedCount; i++)
	{
		const int comboVars = timedArr[i].seComboVars;
		if (((comboVars >> 24) & 0xFF) == effectType)
		{
			totalSeverity += GetSeverityForTimedItem(&timedArr[i], curTime);
		}
	}

	const StatusEffectEndlessData* endlessArr =
		reinterpret_cast<const StatusEffectEndlessData*>(
			reinterpret_cast<const char*>(entity) + entInfo->endlessOffset);

	for (int i = 0; i < entInfo->endlessCount; i++)
	{
		const int comboVars = endlessArr[i].seComboVars;
		if (comboVars != 0 && ((comboVars >> 24) & 0xFF) == effectType)
		{
			const float severity = static_cast<float>(
				static_cast<unsigned char>(comboVars >> 7)) / 255.0f;
			totalSeverity += severity;
		}
	}

	return totalSeverity;
}

//------------------------------------------------------------------------------
// Native wrappers
// Script: float StatusEffect_GetTotalSeverity(entity ent, int eStatusEffect)
// Stack: [1] = roottable, [2] = entity, [3] = effectType
//------------------------------------------------------------------------------
static SQRESULT ServerScript_StatusEffect_GetTotalSeverity(HSQUIRRELVM v)
{
	void* pEntity = ExtractEntityFromStack(v, 2);
	if (!pEntity)
	{
		v_SQVM_ScriptError("StatusEffect_GetTotalSeverity: entity is null or invalid");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	SQInteger effectType = 0;
	sq_getinteger(v, 3, &effectType);

	if (effectType < 0 || effectType >= STATUS_EFFECT_TYPE_MAX)
	{
		v_SQVM_ScriptError("StatusEffect_GetTotalSeverity: effectType %d out of range (0-%d)",
			static_cast<int>(effectType), STATUS_EFFECT_TYPE_MAX - 1);
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	const float result = GetStatusEffectTotalSeverity(
		pEntity, static_cast<int>(effectType), gpGlobals->curTime, s_serverInfo);

	sq_pushfloat(v, result);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT ClientScript_StatusEffect_GetTotalSeverity(HSQUIRRELVM v)
{
	void* pEntity = ExtractEntityFromStack(v, 2);
	if (!pEntity)
	{
		v_SQVM_ScriptError("StatusEffect_GetTotalSeverity: entity is null or invalid");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	SQInteger effectType = 0;
	sq_getinteger(v, 3, &effectType);

	if (effectType < 0 || effectType >= STATUS_EFFECT_TYPE_MAX)
	{
		v_SQVM_ScriptError("StatusEffect_GetTotalSeverity: effectType %d out of range (0-%d)",
			static_cast<int>(effectType), STATUS_EFFECT_TYPE_MAX - 1);
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	const float result = GetStatusEffectTotalSeverity(
		pEntity, static_cast<int>(effectType), gpGlobals->curTime, s_clientInfo);

	sq_pushfloat(v, result);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT UIScript_StatusEffect_GetTotalSeverity(HSQUIRRELVM v)
{
	void* pEntity = ExtractEntityFromStack(v, 2);
	if (!pEntity)
	{
		v_SQVM_ScriptError("StatusEffect_GetTotalSeverity: entity is null or invalid");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	SQInteger effectType = 0;
	sq_getinteger(v, 3, &effectType);

	if (effectType < 0 || effectType >= STATUS_EFFECT_TYPE_MAX)
	{
		v_SQVM_ScriptError("StatusEffect_GetTotalSeverity: effectType %d out of range (0-%d)",
			static_cast<int>(effectType), STATUS_EFFECT_TYPE_MAX - 1);
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	const float result = GetStatusEffectTotalSeverity(
		pEntity, static_cast<int>(effectType), gpGlobals->curTime, s_clientInfo);

	sq_pushfloat(v, result);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//------------------------------------------------------------------------------
// Registration
//------------------------------------------------------------------------------
void StatusEffects_SDK_RegisterServerFunctions(CSquirrelVM* vm)
{
	if (!vm)
		return;

	static bool s_initDone = false;
	if (!s_initDone)
	{
		StatusEffects_SDK_Init();
		s_initDone = true;
	}

	Script_RegisterFuncNamed(vm,
		"StatusEffect_GetTotalSeverity",
		"Server_StatusEffect_GetTotalSeverity",
		"Gets the total (summed) severity of all status effects of the given type on an entity",
		"float",
		"entity ent, int statusEffectType",
		false,
		ServerScript_StatusEffect_GetTotalSeverity);
}

void StatusEffects_SDK_RegisterClientFunctions(CSquirrelVM* vm)
{
	if (!vm)
		return;

	Script_RegisterFuncNamed(vm,
		"StatusEffect_GetTotalSeverity",
		"Client_StatusEffect_GetTotalSeverity",
		"Gets the total (summed) severity of all status effects of the given type on an entity",
		"float",
		"entity ent, int statusEffectType",
		false,
		ClientScript_StatusEffect_GetTotalSeverity);
}

void StatusEffects_SDK_RegisterUIFunctions(CSquirrelVM* vm)
{
	if (!vm)
		return;

	Script_RegisterFuncNamed(vm,
		"StatusEffect_GetTotalSeverity",
		"UI_StatusEffect_GetTotalSeverity",
		"Gets the total (summed) severity of all status effects of the given type on an entity",
		"float",
		"entity ent, int statusEffectType",
		false,
		UIScript_StatusEffect_GetTotalSeverity);
}
