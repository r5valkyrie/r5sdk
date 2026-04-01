#ifndef WEAPON_HEAT_H
#define WEAPON_HEAT_H

#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "thirdparty/detours/include/idetour.h"

struct ScriptClassDescriptor_t;

void WeaponHeat_RegisterWeaponFuncs(ScriptClassDescriptor_t* weaponStruct);
void WeaponHeat_LevelShutdown();
void WeaponHeat_UpdateAll();

SQRESULT Script_Global_Weapon_GetBaseClassName(HSQUIRRELVM v);
SQRESULT Script_Global_Weapon_GetBaseClassNameOrEmpty(HSQUIRRELVM v);

// Returns curved charge fraction for a weapon entity (used by RUI tracks)
float WeaponHeat_GetCurvedChargeFraction(void* pWeapon);

// Engine: PlayWeaponEffectNoCull native SQRESULT function (parses args from VM stack)
inline SQRESULT(*v_PlayWeaponEffectNoCull_Native)(HSQUIRRELVM v) = nullptr;

///////////////////////////////////////////////////////////////////////////////
class VWeaponHeat : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("PlayWeaponEffectNoCull_Native", v_PlayWeaponEffectNoCull_Native);
	}
	virtual void GetFun(void) const
	{
		// PlayWeaponEffectNoCull native SQRESULT function
		Module_FindPattern(g_GameDll,
			"40 53 48 83 EC 20 48 8D 54 24 ?? 48 8B D9 E8 ?? ?? ?? ?? 84 C0 74")
			.GetPtr(v_PlayWeaponEffectNoCull_Native);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const { }
};
///////////////////////////////////////////////////////////////////////////////

#endif // WEAPON_HEAT_H
