#ifndef VIEWMODEL_POSEPARAM_H
#define VIEWMODEL_POSEPARAM_H
#include "thirdparty/detours/include/idetour.h"

struct ScriptClassDescriptor_t;

void ViewmodelPoseParam_RegisterClientWeaponFuncs(ScriptClassDescriptor_t* weaponStruct);

inline float g_scriptPoseParamValues[2] = {0.0f, 0.0f};
inline int g_script0PoseParamIndex = -1;
inline int g_script1PoseParamIndex = -1;

inline __int64(*v_C_BaseViewModel_OnModelChanged)(__int64 viewmodel);
inline __int64(*v_C_BaseViewModel_UpdatePoseParameters)(__int64 viewmodel, __int64 player);
inline __int64(*v_LookupPoseParameter)(__int64 entity, __int64 studioHdr, const char* name);
inline void(*v_SetPoseParameter)(__int64 entity, __int64 studioHdr, int index, float value);

///////////////////////////////////////////////////////////////////////////////
class VViewmodelPoseParam : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("C_BaseViewModel::OnModelChanged", v_C_BaseViewModel_OnModelChanged);
		LogFunAdr("C_BaseViewModel::UpdatePoseParameters", v_C_BaseViewModel_UpdatePoseParameters);
		LogFunAdr("LookupPoseParameter", v_LookupPoseParameter);
		LogFunAdr("SetPoseParameter", v_SetPoseParameter);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "40 53 56 57 48 83 EC 20 48 89 6C 24 ?? 48 8B F9 4C 89 64 24")
			.GetPtr(v_C_BaseViewModel_OnModelChanged);

		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 48 89 6C 24 ?? 57 48 83 EC 40 48 83 B9 E0 10 00 00 00 48 8B EA")
			.GetPtr(v_C_BaseViewModel_UpdatePoseParameters);

		Module_FindPattern(g_GameDll, "48 89 6C 24 ?? 56 48 83 EC 20 49 8B E8 48 8B F2 48 85 D2")
			.GetPtr(v_LookupPoseParameter);

		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 57 48 83 EC 30 0F 29 74 24 ?? 0F 28 F3 49 63 F8")
			.GetPtr(v_SetPoseParameter);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // VIEWMODEL_POSEPARAM_H
