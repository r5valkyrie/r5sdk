#ifndef DT_INJECTION_H
#define DT_INJECTION_H

#include "thirdparty/detours/include/idetour.h"
#include "public/dt_common.h"

struct DTInjectedField
{
	const char* name;
	SendPropType type;
	int bits;
	int clientOffset;
	int serverOffset;
};

void DTInject_AddPlayerField(const char* name, SendPropType type, int bits);
void DTInject_AddWeaponField(const char* name, SendPropType type, int bits);

int DTInject_GetPlayerClientOffset(const char* name);
int DTInject_GetPlayerServerOffset(const char* name);
int DTInject_GetWeaponClientOffset(const char* name);
int DTInject_GetWeaponServerOffset(const char* name);

// Context-aware offset helpers — defined in dt_injection.cpp since they
// need SQVM which isn't available in all translation units that include this header.
struct SQVM;
int DTInject_GetPlayerOffset(SQVM* v, const char* name);
int DTInject_GetWeaponOffset(SQVM* v, const char* name);

// Edict dirty-marking global, resolved at init.
inline uintptr_t g_nServerStateAddr = 0;

// Marks an entity's edict dirty so the DT encoder re-encodes it on the next tick.
inline void DTInject_MarkEntityDirty(void* entity)
{
	if (!entity || !g_nServerStateAddr) return;
	uintptr_t serverState = *reinterpret_cast<uintptr_t*>(g_nServerStateAddr);
	if (!serverState) return;

	int16_t edictIdx = *reinterpret_cast<int16_t*>(reinterpret_cast<uintptr_t>(entity) + 0x58);
	if (edictIdx == -1) return;

	uintptr_t edictArray = *reinterpret_cast<uintptr_t*>(serverState + 0x78);
	if (!edictArray) return;

	_InterlockedOr16(
		reinterpret_cast<volatile short*>(edictArray + 2 * edictIdx + 64),
		0x200);
}

inline int DTInject_ReadInt(void* entity, int offset)
{
	if (!entity || offset < 0) return 0;
	return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(entity) + offset);
}

inline void DTInject_WriteInt(void* entity, int offset, int value)
{
	if (!entity || offset < 0) return;
	*reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(entity) + offset) = value;
	DTInject_MarkEntityDirty(entity);
}

// Entity class size pointers
inline int* g_pClientPlayerSize = nullptr;
inline int* g_pClientWeaponSize = nullptr;
inline int* g_pServerPlayerSize = nullptr;
inline int* g_pServerWeaponSize = nullptr;

// DT table pointers
inline void* g_pClientWeaponRecvTable = nullptr;
inline void* g_pClientPlayerRecvTable = nullptr;
inline void* g_pServerWeaponSendTable = nullptr;
inline void* g_pServerPlayerSendTable = nullptr;

// DT init function pointers
inline int(*v_ClientWeaponXInit)() = nullptr;
inline int(*v_ClientPlayerInit)(__int64 a1) = nullptr;
inline int(*v_ServerWeaponXInit)() = nullptr;
inline int(*v_ServerPlayerInit)(__int64 a1) = nullptr;

///////////////////////////////////////////////////////////////////////////////
class VDTInjection : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("ClientWeaponXInit", v_ClientWeaponXInit);
		LogFunAdr("ClientPlayerInit", v_ClientPlayerInit);
		LogFunAdr("ServerWeaponXInit", v_ServerWeaponXInit);
		LogFunAdr("ServerPlayerInit", v_ServerPlayerInit);
		LogVarAdr("ClientPlayerSize", g_pClientPlayerSize);
		LogVarAdr("ClientWeaponSize", g_pClientWeaponSize);
		LogVarAdr("ServerPlayerSize", g_pServerPlayerSize);
		LogVarAdr("ServerWeaponSize", g_pServerWeaponSize);
	}
	virtual void GetFun(void) const;
	virtual void GetVar(void) const;
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // DT_INJECTION_H
