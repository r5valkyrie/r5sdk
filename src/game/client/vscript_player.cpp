//=============================================================================//
//
// Purpose: Player script functions (combat, shields, stance, offhand,
//          skydive, skyward)
//
//=============================================================================//

#include "core/stdafx.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "vscript/languages/squirrel_re/include/squirrel.h"
#include "vscript/languages/squirrel_re/vsquirrel.h"
#include "game/shared/vscript_gamedll_defs.h"
#include "game/shared/globalnonrewind_vars.h"
#include "game/shared/deathfield_system.h"
#include "game/shared/weapon_script_vars.h"
#include "public/globalvars_base.h"
#include "game/shared/dt_injection.h"
#include "vscript_player.h"

#include <unordered_map>
#include <vector>

extern CGlobalVarsBase* gpGlobals;

//=============================================================================
// Player field offsets
//=============================================================================
static constexpr int CPLAYER_M_PLAYERFLAGS_OFFSET = 0x2CAC;
static constexpr int CPLAYER_M_FORCESTANCE_OFFSET = 0x1A7C;
static constexpr int CPLAYER_M_LASTTIMEDAMAGED_BYPLAYER_OFFSET = 0x2AE0;
static constexpr int CPLAYER_M_LASTTIMEDAMAGED_BYNPC_OFFSET = 0x2AE4;
static constexpr int CPLAYER_M_FREEFALLSTATE_OFFSET = 0x4098;
static constexpr int CPLAYER_M_FREEFALLSTARTTIME_OFFSET = 0x409C;

//=============================================================================
// Skydive aliases (mapped to freefall fields)
//=============================================================================
static SQRESULT Script_Player_IsSkydiving(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
		return SQ_ERROR;

	int state = *(int*)((uintptr_t)pPlayer + CPLAYER_M_FREEFALLSTATE_OFFSET);
	sq_pushbool(v, state != 0);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_Player_IsSkydiveAnticipating(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
		return SQ_ERROR;

	int state = *(int*)((uintptr_t)pPlayer + CPLAYER_M_FREEFALLSTATE_OFFSET);
	sq_pushbool(v, state == 1);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_Player_GetSkydiveStartTime(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
		return SQ_ERROR;

	float startTime = *(float*)((uintptr_t)pPlayer + CPLAYER_M_FREEFALLSTARTTIME_OFFSET);
	sq_pushfloat(v, startTime);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//=============================================================================
// Skyward stubs (jump towers / Valk ult - no engine support)
//=============================================================================
static SQRESULT Script_Player_IsSkywardLaunching(HSQUIRRELVM v)
{
	DevMsg(eDLL_T::CLIENT, "Player_IsSkywardLaunching called - stub\n");
	sq_pushbool(v, false);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_Player_IsSkywardFollowing(HSQUIRRELVM v)
{
	DevMsg(eDLL_T::CLIENT, "Player_IsSkywardFollowing called - stub\n");
	sq_pushbool(v, false);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_Player_IsSkywardDiving(HSQUIRRELVM v)
{
	DevMsg(eDLL_T::CLIENT, "Player_IsSkywardDiving called - stub\n");
	sq_pushbool(v, false);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_Skydive_IsFromUpdraft(HSQUIRRELVM v)
{
	DevMsg(eDLL_T::CLIENT, "Skydive_IsFromUpdraft called - stub\n");
	sq_pushbool(v, false);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_Skydive_IsFromSkywardLaunch(HSQUIRRELVM v)
{
	DevMsg(eDLL_T::CLIENT, "Skydive_IsFromSkywardLaunch called - stub\n");
	sq_pushbool(v, false);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//=============================================================================
// GetLastTimeDamaged
//=============================================================================
static SQRESULT Script_GetLastTimeDamaged(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
		return SQ_ERROR;

	uintptr_t base = reinterpret_cast<uintptr_t>(pPlayer);
	float byPlayer = *(float*)(base + CPLAYER_M_LASTTIMEDAMAGED_BYPLAYER_OFFSET);
	float byNPC = *(float*)(base + CPLAYER_M_LASTTIMEDAMAGED_BYNPC_OFFSET);
	float result = (byPlayer > byNPC) ? byPlayer : byNPC;

	sq_pushfloat(v, result);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//=============================================================================
// Forced stance stack system
// Engine values: 0 = no force, 1 = force stand, 2 = force crouch
//=============================================================================
struct ForcedStanceEntry_t
{
	int handle;
	int stanceType;
};

struct PlayerStanceStack_t
{
	std::vector<ForcedStanceEntry_t> stack;
	int nextHandle = 1;
};

static std::unordered_map<uintptr_t, PlayerStanceStack_t> s_stanceStacks;

static void UpdateForceStanceField(void* pPlayer, PlayerStanceStack_t& ss)
{
	int engineValue = 0;
	if (!ss.stack.empty())
	{
		int scriptStance = ss.stack.back().stanceType;
		engineValue = scriptStance + 1;
	}
	*(int*)((uintptr_t)pPlayer + CPLAYER_M_FORCESTANCE_OFFSET) = engineValue;
}

static SQRESULT Script_PushForcedStance(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
		return SQ_ERROR;

	SQInteger stanceType;
	sq_getinteger(v, 2, &stanceType);

	if (stanceType < 0 || stanceType > 1)
	{
		sq_pushinteger(v, -1);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	auto& ss = s_stanceStacks[reinterpret_cast<uintptr_t>(pPlayer)];
	int handle = ss.nextHandle++;

	ForcedStanceEntry_t entry;
	entry.handle = handle;
	entry.stanceType = static_cast<int>(stanceType);
	ss.stack.push_back(entry);

	UpdateForceStanceField(pPlayer, ss);

	sq_pushinteger(v, handle);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_RemoveForcedStance(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
		return SQ_ERROR;

	SQInteger handle;
	sq_getinteger(v, 2, &handle);

	auto it = s_stanceStacks.find(reinterpret_cast<uintptr_t>(pPlayer));
	if (it != s_stanceStacks.end())
	{
		auto& stack = it->second.stack;
		for (auto sit = stack.begin(); sit != stack.end(); ++sit)
		{
			if (sit->handle == static_cast<int>(handle))
			{
				stack.erase(sit);
				break;
			}
		}
		UpdateForceStanceField(pPlayer, it->second);
	}

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//=============================================================================
// Extra shield system
//=============================================================================
static SQRESULT Script_GetExtraShieldHealth(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
		return SQ_ERROR;

	const int offset = DTInject_GetPlayerOffset(v, "m_extraShieldHealth");
	sq_pushinteger(v, DTInject_ReadInt(pPlayer, offset));
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_GetExtraShieldTier(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
		return SQ_ERROR;

	const int offset = DTInject_GetPlayerOffset(v, "m_extraShieldTier");
	sq_pushinteger(v, DTInject_ReadInt(pPlayer, offset));
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_SetExtraShieldHealth(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
		return SQ_ERROR;

	SQInteger val;
	sq_getinteger(v, 2, &val);

	if (val < 0) val = 0;

	const int offset = DTInject_GetPlayerOffset(v, "m_extraShieldHealth");
	DTInject_WriteInt(pPlayer, offset, static_cast<int>(val));
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_SetExtraShieldTier(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
		return SQ_ERROR;

	SQInteger val;
	sq_getinteger(v, 2, &val);

	if (val < 0 || val > 1023)
		val = (val < 0) ? 0 : 1023;

	const int offset = DTInject_GetPlayerOffset(v, "m_extraShieldTier");
	DTInject_WriteInt(pPlayer, offset, static_cast<int>(val));
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//=============================================================================
// IsConnectionActive
//=============================================================================
static SQRESULT Script_IsConnectionActive(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;

	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
		return SQ_ERROR;

	const int playerFlags = *reinterpret_cast<int*>(
		reinterpret_cast<char*>(pPlayer) + CPLAYER_M_PLAYERFLAGS_OFFSET);

	sq_pushbool(v, (playerFlags & 2) == 0);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//=============================================================================
// Shield change source tracking
//=============================================================================
static constexpr int SHIELD_HISTORY_SIZE = 16;
static constexpr int SHIELD_SOURCE_COUNT = 2;

struct ShieldChangeEntry_t
{
	float time = 0.0f;
	float newShieldHealth = 0.0f;
	int changePerSource[SHIELD_SOURCE_COUNT] = {};
};

struct PlayerShieldHistory_t
{
	ShieldChangeEntry_t history[SHIELD_HISTORY_SIZE] = {};
	int currentIdx = 0;
};

static std::unordered_map<uintptr_t, PlayerShieldHistory_t> s_shieldHistory;

static SQRESULT Script_SetShieldHealthFromSource(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
		return SQ_ERROR;

	SQInteger newHealth;
	sq_getinteger(v, 2, &newHealth);

	SQInteger source;
	sq_getinteger(v, 3, &source);

	if (source < 0 || source >= SHIELD_SOURCE_COUNT)
		source = 0;

	auto& hist = s_shieldHistory[reinterpret_cast<uintptr_t>(pPlayer)];
	int idx = hist.currentIdx % SHIELD_HISTORY_SIZE;
	hist.history[idx].time = gpGlobals ? gpGlobals->curTime : 0.0f;
	hist.history[idx].newShieldHealth = static_cast<float>(newHealth);
	hist.history[idx].changePerSource[0] = 0;
	hist.history[idx].changePerSource[1] = 0;
	hist.history[idx].changePerSource[source] = 1;
	hist.currentIdx++;

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_IsMostRecentShieldChangeFromSingleSource(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
		return SQ_ERROR;

	SQInteger sourceType;
	sq_getinteger(v, 2, &sourceType);

	SQInteger curShieldAmount;
	sq_getinteger(v, 3, &curShieldAmount);

	SQFloat timeThreshold;
	sq_getfloat(v, 4, &timeThreshold);

	auto it = s_shieldHistory.find(reinterpret_cast<uintptr_t>(pPlayer));
	if (it == s_shieldHistory.end())
	{
		sq_pushbool(v, true);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	const auto& hist = it->second;
	const float curTime = gpGlobals ? gpGlobals->curTime : 0.0f;
	const float targetHealth = static_cast<float>(curShieldAmount);

	int startIdx = (hist.currentIdx + SHIELD_HISTORY_SIZE - 1) % SHIELD_HISTORY_SIZE;
	for (int n = 0; n < SHIELD_HISTORY_SIZE; n++)
	{
		int idx = (startIdx - n + SHIELD_HISTORY_SIZE) % SHIELD_HISTORY_SIZE;
		const auto& entry = hist.history[idx];

		if (entry.newShieldHealth != targetHealth)
			continue;

		float elapsed = curTime - entry.time;
		if (elapsed > static_cast<float>(timeThreshold) || elapsed < 0.0f)
			continue;

		for (int s = 0; s < SHIELD_SOURCE_COUNT; s++)
		{
			if (s != static_cast<int>(sourceType) && entry.changePerSource[s] != 0)
			{
				sq_pushbool(v, false);
				SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
			}
		}
	}

	sq_pushbool(v, true);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//=============================================================================
// TrySelectOffhand
//=============================================================================
static SQRESULT Script_TrySelectOffhand(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
		return SQ_ERROR;

	SQInteger offhandIndex;
	sq_getinteger(v, 2, &offhandIndex);

	if (offhandIndex > 7)
	{
		Warning(eDLL_T::SERVER, "TrySelectOffhand: offhand index %d is not valid\n",
			static_cast<int>(offhandIndex));
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	if (g_pOffhandCommandByte)
		*g_pOffhandCommandByte = static_cast<uint8_t>(offhandIndex);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//=============================================================================
// Registration
//=============================================================================
void Script_RegisterPlayerScriptFunctions(ScriptClassDescriptor_t* playerStruct)
{
	playerStruct->AddFunction(
		"IsConnectionActive",
		"Script_IsConnectionActive",
		"Returns true if the player's network connection is active",
		"bool",
		"",
		false,
		Script_IsConnectionActive);

	playerStruct->AddFunction(
		"TrySelectOffhand",
		"Script_TrySelectOffhand",
		"Requests the engine to select an offhand weapon by index",
		"void",
		"int offhandIndex",
		false,
		Script_TrySelectOffhand);

	playerStruct->AddFunction(
		"DeathFieldIndex",
		"Script_DeathFieldIndex",
		"Gets the deathfield index for this player",
		"int",
		"",
		false,
		Script_DeathFieldIndex);

	playerStruct->AddFunction(
		"SetDeathFieldIndex",
		"Script_SetDeathFieldIndex",
		"Sets the deathfield index for this player",
		"void",
		"int deathFieldIndex",
		false,
		Script_SetDeathFieldIndex);

	playerStruct->AddFunction(
		"SetShieldHealthFromSource",
		"Script_SetShieldHealthFromSource",
		"Sets shield health and records the change source",
		"void",
		"int newHealth, int source",
		false,
		Script_SetShieldHealthFromSource);

	playerStruct->AddFunction(
		"IsMostRecentShieldChangeFromSingleSource",
		"Script_IsMostRecentShieldChangeFromSingleSource",
		"Checks if most recent shield change matching the amount came from a single source",
		"bool",
		"int sourceType, int currentShieldHealth, float timeDelta",
		false,
		Script_IsMostRecentShieldChangeFromSingleSource);

	playerStruct->AddFunction(
		"GetExtraShieldHealth", "Script_GetExtraShieldHealth",
		"Gets the player's extra shield health", "int", "", false,
		Script_GetExtraShieldHealth);

	playerStruct->AddFunction(
		"GetExtraShieldTier", "Script_GetExtraShieldTier",
		"Gets the player's extra shield tier", "int", "", false,
		Script_GetExtraShieldTier);

	playerStruct->AddFunction(
		"SetExtraShieldHealth", "Script_SetExtraShieldHealth",
		"Sets the player's extra shield health", "void", "int value", false,
		Script_SetExtraShieldHealth);

	playerStruct->AddFunction(
		"SetExtraShieldTier", "Script_SetExtraShieldTier",
		"Sets the player's extra shield tier", "void", "int value", false,
		Script_SetExtraShieldTier);

	playerStruct->AddFunction(
		"PushForcedStance", "Script_PushForcedStance",
		"Forces player into a stance, returns handle for removal", "int", "int stanceType", false,
		Script_PushForcedStance);

	playerStruct->AddFunction(
		"RemoveForcedStance", "Script_RemoveForcedStance",
		"Removes a previously pushed forced stance by handle", "void", "int handle", false,
		Script_RemoveForcedStance);

	playerStruct->AddFunction(
		"GetLastTimeDamaged", "Script_GetLastTimeDamaged",
		"Returns the last time the player was damaged by any source", "float", "", false,
		Script_GetLastTimeDamaged);

	playerStruct->AddFunction(
		"GetNonRewindRespawnTime", "Script_GetNonRewindRespawnTime",
		"Gets the non-rewind respawn time for this player", "float", "", false,
		Script_GetNonRewindRespawnTime);

	playerStruct->AddFunction(
		"GetNonRewindMusicPack", "Script_GetNonRewindMusicPack",
		"Gets the non-rewind music pack for this player", "int", "", false,
		Script_GetNonRewindMusicPack);

	playerStruct->AddFunction(
		"Player_IsSkydiving", "Script_Player_IsSkydiving",
		"Returns true if the player is skydiving", "bool", "", false,
		Script_Player_IsSkydiving);

	playerStruct->AddFunction(
		"Player_IsSkydiveAnticipating", "Script_Player_IsSkydiveAnticipating",
		"Returns true if the player is about to land from skydive", "bool", "", false,
		Script_Player_IsSkydiveAnticipating);

	playerStruct->AddFunction(
		"Player_GetSkydiveStartTime", "Script_Player_GetSkydiveStartTime",
		"Returns the time skydive started", "float", "", false,
		Script_Player_GetSkydiveStartTime);

	playerStruct->AddFunction(
		"Player_IsSkywardLaunching", "Script_Player_IsSkywardLaunching",
		"Returns true if the player is skyward launching", "bool", "", false,
		Script_Player_IsSkywardLaunching);

	playerStruct->AddFunction(
		"Player_IsSkywardFollowing", "Script_Player_IsSkywardFollowing",
		"Returns true if the player is following a skyward launch", "bool", "", false,
		Script_Player_IsSkywardFollowing);

	playerStruct->AddFunction(
		"Player_IsSkywardDiving", "Script_Player_IsSkywardDiving",
		"Returns true if the player is skyward diving", "bool", "", false,
		Script_Player_IsSkywardDiving);

	playerStruct->AddFunction(
		"Skydive_IsFromUpdraft", "Script_Skydive_IsFromUpdraft",
		"Returns true if skydive was triggered by an updraft", "bool", "", false,
		Script_Skydive_IsFromUpdraft);

	playerStruct->AddFunction(
		"Skydive_IsFromSkywardLaunch", "Script_Skydive_IsFromSkywardLaunch",
		"Returns true if skydive was triggered by a skyward launch", "bool", "", false,
		Script_Skydive_IsFromSkywardLaunch);
}

void Script_RegisterPlayerScriptSetters(ScriptClassDescriptor_t* playerStruct)
{
	playerStruct->AddFunction(
		"SetNonRewindRespawnTime", "Script_SetNonRewindRespawnTime",
		"Sets the non-rewind respawn time for this player", "void", "float time", false,
		Script_SetNonRewindRespawnTime);

	playerStruct->AddFunction(
		"SetNonRewindMusicPack", "Script_SetNonRewindMusicPack",
		"Sets the non-rewind music pack for this player", "void", "int pack", false,
		Script_SetNonRewindMusicPack);
}

void VScriptPlayer_LevelShutdown()
{
	s_stanceStacks.clear();
	s_shieldHistory.clear();
}

int VScriptPlayer_GetExtraShieldHealth(void* pPlayer)
{
	return DTInject_ReadInt(pPlayer, DTInject_GetPlayerClientOffset("m_extraShieldHealth"));
}

int VScriptPlayer_GetExtraShieldTier(void* pPlayer)
{
	return DTInject_ReadInt(pPlayer, DTInject_GetPlayerClientOffset("m_extraShieldTier"));
}
