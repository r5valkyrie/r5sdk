//=============================================================================//
//
// Purpose: Weapon heat mechanic system
//
// Tracks weapon heat buildup from firing and natural decay over time.
// Heat config is loaded from weapon .txt files (KeyValues format).
// Updates are script-driven to avoid iterating stale entity pointers.
//
//=============================================================================//

#include "core/stdafx.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "vscript/languages/squirrel_re/vsquirrel.h"
#include "vscript/languages/squirrel_re/include/squirrel.h"
#include "game/shared/vscript_gamedll_defs.h"
#include "public/globalvars_base.h"
#include "weapon_heat.h"
#include "weapon_script_vars.h"

#ifndef CLIENT_DLL
#include "public/edict.h"
#include "game/server/r1/weapon_x.h"
#else
#include "game/client/r1/c_weapon_x.h"
#include "game/client/viewmodel_poseparam.h"
#endif

#include <unordered_map>
#include <string>
#include <fstream>

#ifndef CLIENT_DLL
extern CGlobalVars* gpGlobals;
#else
extern CGlobalVarsBase* gpGlobals;
#endif

//-----------------------------------------------------------------------------
// Entity offsets
//-----------------------------------------------------------------------------
static constexpr int WEAPON_CLASSNAME_OFFSET = 0x16C0;
static constexpr int WEAPON_LAST_PRIMARY_ATTACK_OFFSET = 0x1304;
static constexpr int WEAPON_MODVARS_OFFSET = 0x133C;
static constexpr int MODVAR_BURST_FIRE_DELAY_OFFSET = 0x34;
static constexpr int WEAPON_CHARGE_START_TIME_OFFSET = 0x164C;
static constexpr int WEAPON_IS_CHARGING_OFFSET = 0x1661;
static constexpr int MODVAR_CHARGE_TIME_OFFSET = 0x4C0;

//-----------------------------------------------------------------------------
// Per-weapon-class heat configuration
//-----------------------------------------------------------------------------
struct WeaponHeatConfig
{
	bool hasHeatDecay = false;
	float heatPerBullet = 0.0f;
	float heatDecayTime = 0.0f;
	float heatDecayDelay = 0.0f;

	bool hasHeatModVar0 = false;
	int heatModVar0Offset = -1;
	float heatModVar0Start = 0.0f;
	float heatModVar0End = 0.0f;

	std::string weaponBaseClass;

	bool hasChargeCurve = false;
	float chargeCurveA = 0.0f;
	float chargeCurveB = 0.0f;
	float chargeCurveC = 0.0f;
};

//-----------------------------------------------------------------------------
// Per-weapon-instance heat state
//-----------------------------------------------------------------------------
struct WeaponHeatState
{
	float heatValue = 0.0f;
	float heatValueOnLastFire = 0.0f;
	bool fullyHeated = false;
	bool fullyHeatedLastFrame = false;
	float lastPrimaryAttackTime = 0.0f;
};

static std::unordered_map<uintptr_t, WeaponHeatState> s_weaponHeatStates;
static std::unordered_map<std::string, WeaponHeatConfig> s_weaponHeatConfigs;
static float s_lastActiveHeatValue = 0.0f;
static constexpr size_t MAX_WEAPON_HEAT_STATES = 256;
static constexpr size_t MAX_WEAPON_HEAT_CONFIGS = 256;

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------
static inline float Clamp01(float val)
{
	if (val < 0.0f) return 0.0f;
	if (val > 1.0f) return 1.0f;
	return val;
}

static inline const char* GetWeaponClassName(void* pWeapon)
{
	return (const char*)((uintptr_t)pWeapon + WEAPON_CLASSNAME_OFFSET);
}

static inline float GetWeaponLastPrimaryAttack(void* pWeapon)
{
	return *(float*)((uintptr_t)pWeapon + WEAPON_LAST_PRIMARY_ATTACK_OFFSET);
}

static inline float* GetWeaponModVarFloat(void* pWeapon, int modVarByteOffset)
{
	return (float*)((uintptr_t)pWeapon + WEAPON_MODVARS_OFFSET + modVarByteOffset);
}

static int ResolveModVarOffset(const std::string& modVarName)
{
	if (modVarName == "burst_fire_delay")
		return MODVAR_BURST_FIRE_DELAY_OFFSET;

	Warning(eDLL_T::CLIENT, "WeaponHeat: Unknown heat_mod_var0 '%s' - interpolation disabled\n",
		modVarName.c_str());
	return -1;
}

static WeaponHeatState& GetHeatState(void* pWeapon)
{
	const uintptr_t key = reinterpret_cast<uintptr_t>(pWeapon);
	auto it = s_weaponHeatStates.find(key);
	if (it != s_weaponHeatStates.end())
		return it->second;

	if (s_weaponHeatStates.size() >= MAX_WEAPON_HEAT_STATES)
	{
		// Evict oldest entries (clear and start fresh)
		s_weaponHeatStates.clear();
	}
	return s_weaponHeatStates[key];
}

//-----------------------------------------------------------------------------
// Weapon .txt file parser - extracts heat-specific keys
//-----------------------------------------------------------------------------
static void TrimWhitespace(std::string& s)
{
	size_t start = s.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) { s.clear(); return; }
	size_t end = s.find_last_not_of(" \t\r\n");
	s = s.substr(start, end - start + 1);
}

static bool ParseQuotedString(const std::string& line, size_t& pos, std::string& out)
{
	size_t q1 = line.find('"', pos);
	if (q1 == std::string::npos) return false;
	size_t q2 = line.find('"', q1 + 1);
	if (q2 == std::string::npos) return false;
	out = line.substr(q1 + 1, q2 - q1 - 1);
	pos = q2 + 1;
	return true;
}

static float SafeStof(const std::string& s, float fallback = 0.0f)
{
	try { return std::stof(s); }
	catch (...) { return fallback; }
}

static int SafeStoi(const std::string& s, int fallback = 0)
{
	try { return std::stoi(s); }
	catch (...) { return fallback; }
}

static WeaponHeatConfig LoadHeatConfigFromFile(const std::string& weaponClassName)
{
	WeaponHeatConfig config;

	// Reject path traversal attempts in weapon classnames
	if (weaponClassName.find("..") != std::string::npos ||
		weaponClassName.find('/') != std::string::npos ||
		weaponClassName.find('\\') != std::string::npos)
	{
		Warning(eDLL_T::CLIENT, "WeaponHeat: Rejected invalid weapon classname '%s'\n",
			weaponClassName.c_str());
		return config;
	}

	const char* searchPaths[] = {
		"platform/scripts/weapons/",
		"platform/scripts_0/weapons/"
	};

	std::ifstream file;
	for (const char* basePath : searchPaths)
	{
		std::string path = std::string(basePath) + weaponClassName + ".txt";
		file.open(path);
		if (file.is_open())
			break;
	}

	if (!file.is_open())
		return config;

	std::string line;
	std::string heatModVar0Name;

	while (std::getline(file, line))
	{
		size_t commentPos = line.find("//");
		if (commentPos != std::string::npos)
			line = line.substr(0, commentPos);

		TrimWhitespace(line);
		if (line.empty())
			continue;

		size_t pos = 0;
		std::string key, value;
		if (!ParseQuotedString(line, pos, key))
			continue;
		if (!ParseQuotedString(line, pos, value))
			continue;

		if (key == "has_heat_decay")
			config.hasHeatDecay = (SafeStoi(value) != 0);
		else if (key == "heat_per_bullet")
			config.heatPerBullet = SafeStof(value);
		else if (key == "heat_decay_time")
			config.heatDecayTime = SafeStof(value);
		else if (key == "heat_decay_delay")
			config.heatDecayDelay = SafeStof(value);
		else if (key == "heat_mod_var0")
			heatModVar0Name = value;
		else if (key == "heat_mod_var0_start")
			config.heatModVar0Start = SafeStof(value);
		else if (key == "heat_mod_var0_end")
			config.heatModVar0End = SafeStof(value);
		else if (key == "weaponBaseClass")
			config.weaponBaseClass = value;
		else if (key == "charge_curve_coefficients")
		{
			// Parse "a b c" space-separated floats
			float a = 0.0f, b = 0.0f, c = 0.0f;
			if (sscanf(value.c_str(), "%f %f %f", &a, &b, &c) == 3)
			{
				config.chargeCurveA = a;
				config.chargeCurveB = b;
				config.chargeCurveC = c;
				config.hasChargeCurve = true;
			}
		}
	}

	file.close();

	if (config.hasHeatDecay && !heatModVar0Name.empty())
	{
		config.heatModVar0Offset = ResolveModVarOffset(heatModVar0Name);
		config.hasHeatModVar0 = (config.heatModVar0Offset >= 0);
	}

	if (config.hasHeatDecay)
	{
		DevMsg(eDLL_T::CLIENT,
			"WeaponHeat: Loaded '%s' - perBullet=%.5f, decayTime=%.1f, decayDelay=%.1f\n",
			weaponClassName.c_str(), config.heatPerBullet, config.heatDecayTime, config.heatDecayDelay);
	}

	return config;
}

static const WeaponHeatConfig& GetHeatConfig(const char* weaponClassName)
{
	if (!weaponClassName || !*weaponClassName)
	{
		static WeaponHeatConfig s_empty;
		return s_empty;
	}

	std::string name(weaponClassName);
	auto it = s_weaponHeatConfigs.find(name);
	if (it != s_weaponHeatConfigs.end())
		return it->second;

	if (s_weaponHeatConfigs.size() >= MAX_WEAPON_HEAT_CONFIGS)
	{
		static WeaponHeatConfig s_empty;
		return s_empty;
	}

	WeaponHeatConfig config = LoadHeatConfigFromFile(name);
	auto result = s_weaponHeatConfigs.emplace(name, config);
	return result.first->second;
}

//-----------------------------------------------------------------------------
// Core heat update - fire detection, accumulation, and decay
//-----------------------------------------------------------------------------
static void UpdateHeatForWeapon(void* pWeapon, WeaponHeatState& state, const WeaponHeatConfig& config)
{
	if (!gpGlobals || !config.hasHeatDecay)
		return;

	const float curTime = gpGlobals->curTime;
	const float engineLastAttack = GetWeaponLastPrimaryAttack(pWeapon);

	if (engineLastAttack != state.lastPrimaryAttackTime && engineLastAttack > 0.0f)
	{
		state.heatValue = Clamp01(state.heatValue + config.heatPerBullet);
		state.heatValueOnLastFire = state.heatValue;
		state.lastPrimaryAttackTime = engineLastAttack;
	}

	// Decay: elapsed -> decayProgress -> subtract from snapshot
	if (state.lastPrimaryAttackTime > 0.0f && config.heatDecayTime > 0.0f)
	{
		const float elapsed = curTime - state.lastPrimaryAttackTime;
		const float decayProgress = Clamp01((elapsed - config.heatDecayDelay) / config.heatDecayTime);
		state.heatValue = Clamp01(state.heatValueOnLastFire - decayProgress);
	}

	state.fullyHeatedLastFrame = state.fullyHeated;
	state.fullyHeated = (state.heatValue >= 1.0f);

	// Interpolate modVar0 by heat (e.g. burst_fire_delay)
	if (config.hasHeatModVar0)
	{
		float* modVarPtr = GetWeaponModVarFloat(pWeapon, config.heatModVar0Offset);
		*modVarPtr = (config.heatModVar0End - config.heatModVar0Start) * state.heatValue
					+ config.heatModVar0Start;
	}

	WeaponScriptVars_SetScriptFloat0(pWeapon, state.heatValue);
	s_lastActiveHeatValue = state.heatValue;
}

//-----------------------------------------------------------------------------
// Script natives
//-----------------------------------------------------------------------------
static SQRESULT Script_HasHeatDecay(HSQUIRRELVM v)
{
	void* pWeapon = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pWeapon)))
		return SQ_ERROR;

	const char* className = GetWeaponClassName(pWeapon);
	const WeaponHeatConfig& config = GetHeatConfig(className);

	sq_pushbool(v, config.hasHeatDecay);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_GetHeatValue(HSQUIRRELVM v)
{
	void* pWeapon = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pWeapon)))
		return SQ_ERROR;

	const char* className = GetWeaponClassName(pWeapon);
	const WeaponHeatConfig& config = GetHeatConfig(className);
	WeaponHeatState& state = GetHeatState(pWeapon);

	UpdateHeatForWeapon(pWeapon, state, config);

	sq_pushfloat(v, state.heatValue);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_GetHeatValueOnLastFire(HSQUIRRELVM v)
{
	void* pWeapon = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pWeapon)))
		return SQ_ERROR;

	const char* className = GetWeaponClassName(pWeapon);
	const WeaponHeatConfig& config = GetHeatConfig(className);
	WeaponHeatState& state = GetHeatState(pWeapon);

	UpdateHeatForWeapon(pWeapon, state, config);

	sq_pushfloat(v, state.heatValueOnLastFire);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_SetHeatValue(HSQUIRRELVM v)
{
	void* pWeapon = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pWeapon)))
		return SQ_ERROR;

	SQFloat value;
	sq_getfloat(v, 2, &value);

	const float clamped = Clamp01(static_cast<float>(value));
	WeaponHeatState& state = GetHeatState(pWeapon);
	state.heatValueOnLastFire = clamped;
	state.heatValue = clamped;

	WeaponScriptVars_SetScriptFloat0(pWeapon, clamped);
	s_lastActiveHeatValue = clamped;

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Weapon base class functions
//-----------------------------------------------------------------------------
static SQRESULT Script_WeaponGetBaseClassName(HSQUIRRELVM v)
{
	void* pWeapon = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pWeapon)))
		return SQ_ERROR;

	const char* className = GetWeaponClassName(pWeapon);
	const WeaponHeatConfig& config = GetHeatConfig(className);

	// If has baseclass, return it; otherwise return the weapon's own classname
	if (!config.weaponBaseClass.empty())
		sq_pushstring(v, config.weaponBaseClass.c_str(), -1);
	else
		sq_pushstring(v, className ? className : "", -1);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_WeaponGetBaseClassNameOrEmpty(HSQUIRRELVM v)
{
	void* pWeapon = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pWeapon)))
		return SQ_ERROR;

	const char* className = GetWeaponClassName(pWeapon);
	const WeaponHeatConfig& config = GetHeatConfig(className);

	// If has baseclass, return it; otherwise return empty string
	if (!config.weaponBaseClass.empty())
		sq_pushstring(v, config.weaponBaseClass.c_str(), -1);
	else
		sq_pushstring(v, "", -1);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_WeaponHasBaseClass(HSQUIRRELVM v)
{
	void* pWeapon = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pWeapon)))
		return SQ_ERROR;

	const char* className = GetWeaponClassName(pWeapon);
	const WeaponHeatConfig& config = GetHeatConfig(className);

	sq_pushbool(v, !config.weaponBaseClass.empty());
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

SQRESULT Script_Global_Weapon_GetBaseClassName(HSQUIRRELVM v)
{
	const SQChar* weaponName = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &weaponName)) || !weaponName)
		return SQ_ERROR;

	const WeaponHeatConfig& config = GetHeatConfig(weaponName);

	if (!config.weaponBaseClass.empty())
		sq_pushstring(v, config.weaponBaseClass.c_str(), -1);
	else
		sq_pushstring(v, weaponName, -1);

	return SQ_OK;
}

SQRESULT Script_Global_Weapon_GetBaseClassNameOrEmpty(HSQUIRRELVM v)
{
	const SQChar* weaponName = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &weaponName)) || !weaponName)
		return SQ_ERROR;

	const WeaponHeatConfig& config = GetHeatConfig(weaponName);

	if (!config.weaponBaseClass.empty())
		sq_pushstring(v, config.weaponBaseClass.c_str(), -1);
	else
		sq_pushstring(v, "", -1);

	return SQ_OK;
}

//-----------------------------------------------------------------------------
// Charge curve system
//-----------------------------------------------------------------------------
static inline float GetWeaponChargeStartTime(void* pWeapon)
{
	return *(float*)((uintptr_t)pWeapon + WEAPON_CHARGE_START_TIME_OFFSET);
}

static inline bool GetWeaponIsCharging(void* pWeapon)
{
	return *(bool*)((uintptr_t)pWeapon + WEAPON_IS_CHARGING_OFFSET);
}

static inline float GetWeaponChargeTime(void* pWeapon)
{
	return *(float*)((uintptr_t)pWeapon + WEAPON_MODVARS_OFFSET + MODVAR_CHARGE_TIME_OFFSET);
}

static SQRESULT Script_GetWeaponChargeFractionCurved(HSQUIRRELVM v)
{
	void* pWeapon = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pWeapon)))
		return SQ_ERROR;

	const char* className = GetWeaponClassName(pWeapon);
	const WeaponHeatConfig& config = GetHeatConfig(className);

	if (!config.hasChargeCurve)
	{
		sq_pushfloat(v, 0.0f);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	float chargeTime = GetWeaponChargeTime(pWeapon);
	if (chargeTime <= 0.0f)
	{
		sq_pushfloat(v, 0.0f);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	// Compute linear charge fraction
	float x = 0.0f;
	if (GetWeaponIsCharging(pWeapon) && gpGlobals)
	{
		float chargeStartTime = GetWeaponChargeStartTime(pWeapon);
		x = (gpGlobals->curTime - chargeStartTime) / chargeTime;
		x = Clamp01(x);
	}

	// Apply cubic curve: result = clamp(a*x³ + b*x² + c*x, 0, 1)
	float a = config.chargeCurveA;
	float b = config.chargeCurveB;
	float c = config.chargeCurveC;
	float result = Clamp01(((a * x + b) * x + c) * x);

	sq_pushfloat(v, result);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Per-frame viewmodel sync
//-----------------------------------------------------------------------------
void WeaponHeat_UpdateAll()
{
#ifdef CLIENT_DLL
	g_scriptPoseParamValues[0] = s_lastActiveHeatValue;
#endif
}

void WeaponHeat_LevelShutdown()
{
	s_weaponHeatStates.clear();
	s_weaponHeatConfigs.clear();
	s_lastActiveHeatValue = 0.0f;
}

//-----------------------------------------------------------------------------
// Curved charge fraction for RUI tracks (called from ruitracks.cpp)
//-----------------------------------------------------------------------------
float WeaponHeat_GetCurvedChargeFraction(void* pWeapon)
{
	if (!pWeapon || !gpGlobals)
		return 0.0f;

	const char* className = GetWeaponClassName(pWeapon);
	if (!className || !*className)
		return 0.0f;

	const WeaponHeatConfig& config = GetHeatConfig(className);
	if (!config.hasChargeCurve)
		return 0.0f;

	float chargeTime = GetWeaponChargeTime(pWeapon);
	if (chargeTime <= 0.0f)
		return 0.0f;

	float x = 0.0f;
	if (GetWeaponIsCharging(pWeapon))
	{
		float chargeStartTime = GetWeaponChargeStartTime(pWeapon);
		x = (gpGlobals->curTime - chargeStartTime) / chargeTime;
		x = Clamp01(x);
	}

	float a = config.chargeCurveA;
	float b = config.chargeCurveB;
	float c = config.chargeCurveC;
	return Clamp01(((a * x + b) * x + c) * x);
}

//-----------------------------------------------------------------------------
// PlayWeaponEffectNoCullReturnViewEffectHandle
// Calls engine's PlayWeaponEffectNoCull to play the effect, then pushes
// an int result. The engine native is a raw SQRESULT function that parses
// its own args from the VM stack (same args as PlayWeaponEffectNoCull).
// We call it, pop its void result, and push -1 as the handle since
// the engine lacks per-effect handle tracking.
//-----------------------------------------------------------------------------
static SQRESULT Script_PlayWeaponEffectNoCullReturnViewEffectHandle(HSQUIRRELVM v)
{
	if (v_PlayWeaponEffectNoCull_Native)
	{
		// The engine native parses args directly from the VM stack
		SQRESULT res = v_PlayWeaponEffectNoCull_Native(v);
		if (SQ_FAILED(res))
			return res;
	}

	// Engine native pushes no return value (void).
	// We push -1 as handle (EffectSetControlPointVector with -1 is a no-op).
	sq_pushinteger(v, -1);
	return SQ_OK;
}

//-----------------------------------------------------------------------------
// Registration
//-----------------------------------------------------------------------------
void WeaponHeat_RegisterWeaponFuncs(ScriptClassDescriptor_t* weaponStruct)
{
	DevMsg(eDLL_T::CLIENT, "[WeaponHeat] Registering weapon heat functions\n");

	weaponStruct->AddFunction("HasHeatDecay", "Script_HasHeatDecay",
		"Returns true if this weapon has the heat decay mechanic", "bool", "", false, Script_HasHeatDecay);

	weaponStruct->AddFunction("GetHeatValue", "Script_GetHeatValue",
		"Returns the current heat value (0.0 - 1.0)", "float", "", false, Script_GetHeatValue);

	weaponStruct->AddFunction("GetHeatValueOnLastFire", "Script_GetHeatValueOnLastFire",
		"Returns the heat value at the time of the last fire", "float", "", false, Script_GetHeatValueOnLastFire);

	weaponStruct->AddFunction("SetHeatValue", "Script_SetHeatValue",
		"Sets the heat value (also sets heat value on last fire)", "void", "float value", false, Script_SetHeatValue);

	weaponStruct->AddFunction("GetWeaponBaseClassName", "Script_WeaponGetBaseClassName",
		"Gets the base class name, or the weapon's own classname if none", "string", "", false, Script_WeaponGetBaseClassName);

	weaponStruct->AddFunction("GetWeaponBaseClassNameOrEmpty", "Script_WeaponGetBaseClassNameOrEmpty",
		"Gets the base class name, or empty string if none", "string", "", false, Script_WeaponGetBaseClassNameOrEmpty);

	weaponStruct->AddFunction("HasBaseClass", "Script_WeaponHasBaseClass",
		"Returns true if this weapon has a base class defined", "bool", "", false, Script_WeaponHasBaseClass);

	weaponStruct->AddFunction("GetWeaponChargeFractionCurved", "Script_GetWeaponChargeFractionCurved",
		"Returns charge fraction with curve applied (0.0 - 1.0)", "float", "", false, Script_GetWeaponChargeFractionCurved);

	weaponStruct->AddFunction("PlayWeaponEffectNoCullReturnViewEffectHandle", "Script_PlayWeaponEffectNoCullReturnViewEffectHandle",
		"Plays a weapon effect and returns the view effect handle", "int",
		"asset effect1p, asset effect3p, string attachPoint, bool usePrimaryAttachPoint, int attachType", false,
		Script_PlayWeaponEffectNoCullReturnViewEffectHandle);
}
