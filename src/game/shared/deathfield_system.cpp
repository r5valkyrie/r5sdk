//=============================================================================//
//
// Purpose: Multi-index deathfield system for realm-based gameplay
//
// Index 0 reads live data from the engine world entity.
// Indexes 1+ are SDK-managed for multi-ring support.
//
//=============================================================================//

#include "core/stdafx.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "vscript/languages/squirrel_re/include/squirrel.h"
#include "game/shared/vscript_gamedll_defs.h"
#include "vscript/languages/squirrel_re/vsquirrel.h"
#include "deathfield_system.h"
#include "dt_injection.h"
#include "public/globalvars_base.h"

#ifndef CLIENT_DLL
#include "public/edict.h"
extern CGlobalVars* gpGlobals;
#else
extern CGlobalVarsBase* gpGlobals;
#endif

#include <cmath>

//-----------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------
static constexpr int MAX_DEATHFIELDS = 16;

// World entity deathfield offsets
static constexpr int WORLD_DF_ISACTIVE     = 0xA40;
static constexpr int WORLD_DF_ORIGIN       = 0xA44;
static constexpr int WORLD_DF_RADIUS_START = 0xA50;
static constexpr int WORLD_DF_RADIUS_END   = 0xA54;
static constexpr int WORLD_DF_TIME_START   = 0xA58;
static constexpr int WORLD_DF_TIME_END     = 0xA5C;

//-----------------------------------------------------------------------------
// Per-deathfield data
//-----------------------------------------------------------------------------
struct DeathFieldData_t
{
	bool isActive = false;
	float originX = 0.0f;
	float originY = 0.0f;
	float originZ = 0.0f;
	float radiusStart = 0.0f;
	float radiusEnd = 0.0f;
	float timeStart = 0.0f;
	float timeEnd = 0.0f;
};

static DeathFieldData_t s_deathFields[MAX_DEATHFIELDS];

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------
static inline void* GetWorldEntity()
{
	if (!g_ppWorldEntity || !*g_ppWorldEntity)
		return nullptr;
	return *g_ppWorldEntity;
}

static float GetCurrentRadius_SideTable(const DeathFieldData_t& df, float time)
{
	if (!df.isActive)
		return 3.4028235e38f;

	if (df.timeEnd <= df.timeStart)
	{
		if ((time - df.timeEnd) < 0.0f)
			return df.radiusStart;
		return df.radiusEnd;
	}

	float t = (time - df.timeStart) / (df.timeEnd - df.timeStart);
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	return df.radiusStart + (df.radiusEnd - df.radiusStart) * t;
}

static float GetCurrentRadius(int index, float time)
{
	if (index == 0 && v_DeathField_GetCurrentRadius)
		return v_DeathField_GetCurrentRadius(time);

	if (index < 0 || index >= MAX_DEATHFIELDS)
		return 3.4028235e38f;

	return GetCurrentRadius_SideTable(s_deathFields[index], time);
}

static bool GetDeathFieldIsActive(int index)
{
	if (index == 0)
	{
		void* worldEnt = GetWorldEntity();
		if (!worldEnt) return false;
		return *(bool*)(reinterpret_cast<uintptr_t>(worldEnt) + WORLD_DF_ISACTIVE);
	}

	if (index < 0 || index >= MAX_DEATHFIELDS)
		return false;

	return s_deathFields[index].isActive;
}

static void GetDeathFieldOrigin(int index, float& outX, float& outY)
{
	if (index == 0)
	{
		void* worldEnt = GetWorldEntity();
		if (!worldEnt) { outX = 0; outY = 0; return; }
		const uintptr_t base = reinterpret_cast<uintptr_t>(worldEnt);
		outX = *(float*)(base + WORLD_DF_ORIGIN);
		outY = *(float*)(base + WORLD_DF_ORIGIN + 4);
		return;
	}

	if (index < 0 || index >= MAX_DEATHFIELDS) { outX = 0; outY = 0; return; }
	outX = s_deathFields[index].originX;
	outY = s_deathFields[index].originY;
}

//-----------------------------------------------------------------------------
// Player DeathFieldIndex
//-----------------------------------------------------------------------------
SQRESULT Script_DeathFieldIndex(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
		return SQ_ERROR;

	const int offset = DTInject_GetPlayerOffset(v, "m_deathFieldIndex");
	sq_pushinteger(v, DTInject_ReadInt(pPlayer, offset));
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

SQRESULT Script_SetDeathFieldIndex(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
		return SQ_ERROR;

	SQInteger index;
	sq_getinteger(v, 2, &index);

	if (index < 0 || index >= MAX_DEATHFIELDS)
	{
		Warning(eDLL_T::SERVER, "SetDeathFieldIndex: index %d out of range [0, %d)\n",
			static_cast<int>(index), MAX_DEATHFIELDS);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	const int offset = DTInject_GetPlayerOffset(v, "m_deathFieldIndex");
	DTInject_WriteInt(pPlayer, offset, static_cast<int>(index));
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Deathfield query functions
//-----------------------------------------------------------------------------
SQRESULT Script_DeathField_IsActive(HSQUIRRELVM v)
{
	SQInteger index;
	sq_getinteger(v, 2, &index);

	sq_pushbool(v, GetDeathFieldIsActive(static_cast<int>(index)));
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

SQRESULT Script_DeathField_PointDistanceFromFrontier(HSQUIRRELVM v)
{
	const SQVector3D* point = nullptr;
	sq_getvector(v, 2, &point);
	if (!point) return SQ_ERROR;

	SQInteger index;
	sq_getinteger(v, 3, &index);

	const int idx = static_cast<int>(index);
	const float curTime = gpGlobals ? gpGlobals->curTime : 0.0f;

	float radius = GetCurrentRadius(idx, curTime);

	float originX, originY;
	GetDeathFieldOrigin(idx, originX, originY);

	float dx = point->x - originX;
	float dy = point->y - originY;
	float dist2D = sqrtf(dx * dx + dy * dy);

	sq_pushfloat(v, radius - dist2D);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

SQRESULT Script_DeathField_GetRadiusForNow(HSQUIRRELVM v)
{
	SQInteger index;
	sq_getinteger(v, 2, &index);

	const float curTime = gpGlobals ? gpGlobals->curTime : 0.0f;
	sq_pushfloat(v, GetCurrentRadius(static_cast<int>(index), curTime));
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

SQRESULT Script_DeathField_GetRadiusForTime(HSQUIRRELVM v)
{
	SQFloat time;
	sq_getfloat(v, 2, &time);

	SQInteger index;
	sq_getinteger(v, 3, &index);

	sq_pushfloat(v, GetCurrentRadius(static_cast<int>(index), static_cast<float>(time)));
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Deathfield configuration (server-only)
//-----------------------------------------------------------------------------
SQRESULT Script_DeathField_SetActive(HSQUIRRELVM v)
{
	SQInteger index;
	sq_getinteger(v, 2, &index);

	SQBool active;
	sq_getbool(v, 3, &active);

	if (index >= 0 && index < MAX_DEATHFIELDS)
		s_deathFields[index].isActive = (active != 0);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

SQRESULT Script_DeathField_SetOrigin(HSQUIRRELVM v)
{
	SQInteger index;
	sq_getinteger(v, 2, &index);

	const SQVector3D* origin = nullptr;
	sq_getvector(v, 3, &origin);
	if (!origin) return SQ_ERROR;

	if (index >= 0 && index < MAX_DEATHFIELDS)
	{
		s_deathFields[index].originX = origin->x;
		s_deathFields[index].originY = origin->y;
		s_deathFields[index].originZ = origin->z;
	}

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

SQRESULT Script_DeathField_SetRadiusStartEnd(HSQUIRRELVM v)
{
	SQInteger index;
	sq_getinteger(v, 2, &index);

	SQFloat start, end;
	sq_getfloat(v, 3, &start);
	sq_getfloat(v, 4, &end);

	if (index >= 0 && index < MAX_DEATHFIELDS)
	{
		s_deathFields[index].radiusStart = static_cast<float>(start);
		s_deathFields[index].radiusEnd = static_cast<float>(end);
	}

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

SQRESULT Script_DeathField_SetTimeStartEnd(HSQUIRRELVM v)
{
	SQInteger index;
	sq_getinteger(v, 2, &index);

	SQFloat start, end;
	sq_getfloat(v, 3, &start);
	sq_getfloat(v, 4, &end);

	if (index >= 0 && index < MAX_DEATHFIELDS)
	{
		s_deathFields[index].timeStart = static_cast<float>(start);
		s_deathFields[index].timeEnd = static_cast<float>(end);
	}

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Level shutdown
//-----------------------------------------------------------------------------
void DeathField_LevelShutdown()
{
	for (int i = 0; i < MAX_DEATHFIELDS; i++)
		s_deathFields[i] = DeathFieldData_t();
}

//-----------------------------------------------------------------------------
// Re-register indexed DeathField functions over engine's no-index versions.
// Called from our detoured VM init orchestrators, after the engine has
// registered its versions but before scripts compile.
//-----------------------------------------------------------------------------
void DeathField_RegisterOnVM(CSquirrelVM* s)
{
	Script_RegisterFuncNamed(s, "DeathField_IsActiveForIndex",
		"Script_DeathField_IsActive",
		"Returns whether a deathfield is active by index",
		"bool", "int deathFieldIndex", false,
		Script_DeathField_IsActive);

	Script_RegisterFuncNamed(s, "DeathField_PointDistanceFromFrontierForIndex",
		"Script_DeathField_PointDistanceFromFrontier",
		"Distance from deathfield frontier by index",
		"float", "vector point, int deathFieldIndex", false,
		Script_DeathField_PointDistanceFromFrontier);

	Script_RegisterFuncNamed(s, "DeathField_GetRadiusForNowForIndex",
		"Script_DeathField_GetRadiusForNow",
		"Gets current deathfield radius by index",
		"float", "int deathFieldIndex", false,
		Script_DeathField_GetRadiusForNow);

	Script_RegisterFuncNamed(s, "DeathField_GetRadiusForTimeForIndex",
		"Script_DeathField_GetRadiusForTime",
		"Gets deathfield radius at given time by index",
		"float", "float time, int deathFieldIndex", false,
		Script_DeathField_GetRadiusForTime);
}

void VDeathFieldSystem::Detour(const bool bAttach) const { }
