#ifndef ENGINE_STATICPROP_BOUNDS_DEBUG_H
#define ENGINE_STATICPROP_BOUNDS_DEBUG_H
#pragma once
#include "tier0/basetypes.h"
#include "thirdparty/detours/include/idetour.h"

//-----------------------------------------------------------------------------
// Debug hook for static prop bounds check function (sub_7FF710844940)
// This function is called during visibility traversal and crashes when
// accessing invalid static prop indices.
//
// Function signature:
//   bool __fastcall StaticPropBoundsCheck(unsigned int staticPropIndex, float* position, float radiusSq)
//
// Globals accessed:
//   qword_7FF711CF4E80 = static prop runtime data (32-byte stride)
//   qword_7FF711CF4EA0 = static prop bounds (24-byte stride)
//   dword_7FF71B92DA5C = static prop threshold (objRef values >= this are static props)
//-----------------------------------------------------------------------------

// Original function pointer
inline bool(*v_StaticPropBoundsCheck)(unsigned int staticPropIndex, float* position, float radiusSq);

// Hook function declaration
bool StaticPropBoundsCheck_Hook(unsigned int staticPropIndex, float* position, float radiusSq);

// Globals for debugging
inline void** g_pStaticPropData = nullptr;       // qword_7FF711CF4E80
inline void** g_pStaticPropBounds = nullptr;     // qword_7FF711CF4EA0
inline int* g_pStaticPropThreshold = nullptr;    // dword_7FF71B92DA5C

// BSP version global - declared as extern in cpp
extern int* g_pBspVersion;  // dword_7FF736C9C6C4

///////////////////////////////////////////////////////////////////////////////
class VStaticPropBoundsDebug : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("StaticPropBoundsCheck", v_StaticPropBoundsCheck);
		LogVarAdr("g_pStaticPropData", g_pStaticPropData);
		LogVarAdr("g_pStaticPropBounds", g_pStaticPropBounds);
		LogVarAdr("g_pStaticPropThreshold", g_pStaticPropThreshold);
		LogVarAdr("g_pBspVersion", g_pBspVersion);
	}
	
	virtual void GetFun(void) const
	{
		// Pattern for sub_7FF710844940:
		// 48 83 EC 48              sub     rsp, 48h
		// F3 0F 10 62 04           movss   xmm4, dword ptr [rdx+4]
		// 0F 57 C0                 xorps   xmm0, xmm0
		// 48 8B 05 ?? ?? ?? ??     mov     rax, cs:qword_7FF711CF4E80
		Module_FindPattern(g_GameDll, "48 83 EC 48 F3 0F 10 62 04 0F 57 C0 48 8B 05").GetPtr(v_StaticPropBoundsCheck);
	}
	
	virtual void GetVar(void) const
	{
		// Get globals from the function
		if (v_StaticPropBoundsCheck)
		{
			// qword_7FF711CF4E80 is at function+0xC with 7-byte RIP-relative addressing
			CMemory funcMem((uintptr_t)v_StaticPropBoundsCheck);
			funcMem.Offset(0xC).ResolveRelativeAddressSelf(3, 7).GetPtr(g_pStaticPropData);
			
			// qword_7FF711CF4EA0 is accessed later in the function
			// Pattern: 48 8B 05 ?? ?? ?? ?? (mov rax, cs:qword_7FF711CF4EA0) around offset 0x48
			funcMem.Offset(0x48).ResolveRelativeAddressSelf(3, 7).GetPtr(g_pStaticPropBounds);
			
			// dword_7FF71B92DA5C - find via pattern "44 2B 0D" = sub r9d, cs:dword_...
			// Pattern: 44 2B 0D ?? ?? ?? ?? 41 8B D9 C1 EB 1F
			// This is the threshold global set during BSP loading
			Module_FindPattern(g_GameDll, "44 2B 0D ?? ?? ?? ?? 41 8B D9 C1 EB 1F").Offset(3).ResolveRelativeAddressSelf(0, 4).GetPtr(g_pStaticPropThreshold);
			
			// dword_7FF736C9C6C4 - BSP version global
			// Pattern: cmp cs:dword_7FF736C9C6C4, 2Eh = 83 3D ?? ?? ?? ?? 2E
			Module_FindPattern(g_GameDll, "83 3D ?? ?? ?? ?? 2E 48 8B 05").Offset(2).ResolveRelativeAddressSelf(0, 5).GetPtr(g_pBspVersion);
		}
	}
	
	virtual void GetCon(void) const { }
	
	virtual void Detour(const bool bAttach) const
	{
		//DetourSetup(&v_StaticPropBoundsCheck, &StaticPropBoundsCheck_Hook, bAttach);
	}
};
///////////////////////////////////////////////////////////////////////////////

#endif // ENGINE_STATICPROP_BOUNDS_DEBUG_H
