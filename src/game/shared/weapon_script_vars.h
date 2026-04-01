#ifndef WEAPON_SCRIPT_VARS_H
#define WEAPON_SCRIPT_VARS_H

#include "thirdparty/detours/include/idetour.h"

struct ScriptClassDescriptor_t;

void WeaponScriptVars_RegisterWeaponFuncs(ScriptClassDescriptor_t* weaponStruct);
void WeaponScriptVars_RegisterEntityFuncs(ScriptClassDescriptor_t* entityStruct);
float WeaponScriptVars_GetScriptFloat0(void* pWeapon);
void WeaponScriptVars_SetScriptFloat0(void* pWeapon, float value);
void WeaponScriptVars_LevelShutdown();
void WeaponScriptVars_PhaseShift_LevelShutdown();
void WeaponScriptVars_WeaponLockedSet_LevelShutdown();
void WeaponScriptVars_RegisterPhaseShiftOverride(ScriptClassDescriptor_t* playerStruct);
void WeaponScriptVars_RegisterWeaponLockedSetSetter(ScriptClassDescriptor_t* weaponStruct);
void WeaponScriptVars_RegisterInfiniteAmmoFuncs(ScriptClassDescriptor_t* weaponStruct);
void WeaponScriptVars_RegisterInfiniteAmmoSetter(ScriptClassDescriptor_t* weaponStruct);
void WeaponScriptVars_InfiniteAmmo_LevelShutdown();

// Engine: CBaseEntity::SetRadius(float) - sets collision cylinder radius
inline void(*v_CBaseEntity_SetRadius)(void* entity, float radius) = nullptr;

// Engine: C_BaseCombatCharacter::PhaseShiftBegin(float warmup, float duration)
// Signature: entity* in rcx, warmup in xmm1, duration in xmm2
inline void(*v_PhaseShiftBegin_Native)(void* entity, float warmup, float duration) = nullptr;

// TrySelectOffhand: resolved global offhand command byte
inline uint8_t* g_pOffhandCommandByte = nullptr;

// Expanded WeaponType enum table (adds "gadget" at index 9)
void WeaponScriptVars_PatchWeaponTypeTable();

///////////////////////////////////////////////////////////////////////////////
class VWeaponScriptVars : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("CBaseEntity::SetRadius", v_CBaseEntity_SetRadius);
		LogFunAdr("PhaseShiftBegin_Native", v_PhaseShiftBegin_Native);
		LogVarAdr("g_pOffhandCommandByte", g_pOffhandCommandByte);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll,
			"48 89 5C 24 ?? 57 48 83 EC 30 F3 0F 10 81 10 0D 00 00")
			.GetPtr(v_CBaseEntity_SetRadius);

		Module_FindPattern(g_GameDll,
			"48 8B 05 ?? ?? ?? ?? F3 0F 58 48 28 F3 0F 11 89 90 17 00 00")
			.GetPtr(v_PhaseShiftBegin_Native);
	}
	virtual void GetVar(void) const
	{
		// TrySelectOffhand: resolve offhand command byte from ActivateOffhandWeaponByIndex
		CMemory wrapper = Module_FindPattern(g_GameDll,
			"83 FA ?? 77 ?? 48 8B 05 ?? ?? ?? ?? 48 8D 0D");
		if (wrapper)
		{
			CMemory leaInstr = wrapper.Offset(12);
			uint8_t* pGlobal = reinterpret_cast<uint8_t*>(
				leaInstr.ResolveRelativeAddress(3, 7).GetPtr());
			if (pGlobal)
				g_pOffhandCommandByte = pGlobal + 0x180;
		}
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const
	{
		if (bAttach)
			WeaponScriptVars_PatchWeaponTypeTable();
	}
};
///////////////////////////////////////////////////////////////////////////////

#endif // WEAPON_SCRIPT_VARS_H
