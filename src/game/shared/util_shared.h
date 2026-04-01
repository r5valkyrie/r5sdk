//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//
#ifndef UTIL_SHARED_H
#define UTIL_SHARED_H
#include "public/engine/IEngineTrace.h"

class CTraceFilterSimple;
typedef bool (*ShouldHitFunc_t)(IHandleEntity* pHandleEntity, int contentsMask);

const char* UTIL_GetEntityScriptInfo(CBaseEntity* pEnt);

inline bool(*v_TraceFilter_ShouldHitEntity)(IHandleEntity* pHandleEntity, const IHandleEntity* pPassEntity, ShouldHitFunc_t extraShouldHitCheckFunc, const int contentsMask, const int traceType);
inline const char*(*v_UTIL_GetEntityScriptInfo)(CBaseEntity* pEnt);

//-----------------------------------------------------------------------------
// traceline methods
//-----------------------------------------------------------------------------
class CTraceFilterSimple : public CTraceFilter
{
public:
	// It does have a base, but we'll never network anything below here..
	//DECLARE_CLASS_NOBASE(CTraceFilterSimple);

	CTraceFilterSimple(const IHandleEntity* pPassEntity, const int collisionGroup, ShouldHitFunc_t pExtraShouldHitCheckFn = NULL);

	virtual bool ShouldHitEntity(IHandleEntity* const pEntity, const int contentsMask);
	virtual bool ShouldBlockTrace(trace_t* const pTrace);

	virtual void SetPassEntity(const IHandleEntity* pPassEntity) { m_pPassEntity = pPassEntity; }
	virtual void SetCollisionGroup(const int iCollisionGroup) { m_collisionGroup = iCollisionGroup; }

	virtual void SetExtraShouldHitFunc(ShouldHitFunc_t shouldHitFunc) { m_pExtraShouldHitCheckFunction = shouldHitFunc; }

	const IHandleEntity* GetPassEntity(void) { return m_pPassEntity; }
	int GetCollisionGroup(void) const { return m_collisionGroup; }

private:
	int m_reserved; // Likely debug-only; no use cases found in the engine.
	const IHandleEntity* m_pPassEntity;
	ShouldHitFunc_t m_pExtraShouldHitCheckFunction;
	int m_collisionGroup;
};

///////////////////////////////////////////////////////////////////////////////
class V_UTIL_Shared : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("TraceFilter_ShouldHitEntity", v_TraceFilter_ShouldHitEntity);
		LogFunAdr("UTIL_GetEntityScriptInfo", v_UTIL_GetEntityScriptInfo);
	}
	virtual void GetFun(void) const { }
	virtual void GetVar(void) const
	{
		Module_FindPattern(g_GameDll, "E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? 48 3B 5F").FollowNearCallSelf().GetPtr(v_TraceFilter_ShouldHitEntity);
		Module_FindPattern(g_GameDll, "E8 ?? ?? ?? ?? 4C 8B 5E ??").FollowNearCallSelf().GetPtr(v_UTIL_GetEntityScriptInfo);
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const { }
};
///////////////////////////////////////////////////////////////////////////////

#endif // !UTIL_SHARED_H
