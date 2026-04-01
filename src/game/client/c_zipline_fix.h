#pragma once
#include "core/stdafx.h"

//-----------------------------------------------------------------------------
// Purpose: Fix for zipline entity validation crash
//
// Crash: EXCEPTION_ACCESS_VIOLATION at r5apex.exe+0xFD2944
// Root cause: sub_7FF7CEBC2940 accesses a1[711] (offset 0xB1C) without NULL check
//
// The function is called during zipline rendering/tracing when:
// 1. Map has zipline entity references (e.g., from newer S17+ maps)
// 2. Entity handles are invalid/uninitialized
// 3. Entity lookup returns NULL
// 4. NULL is passed to validation function which crashes
//
// Call chain:
// sub_7FF7CEBCC800 (CTraceFilterZipline handler)
//   -> Virtual call at vtable offset 0x2E8
//     -> sub_7FF7CEBBCE10 (Zipline entity lookup)
//       -> sub_7FF7CEBC2940(v7) <- v7 is NULL, causes crash!
//
// Fix: Add NULL pointer check before accessing entity data
//-----------------------------------------------------------------------------

// Original function pointer - Entity validation at RVA 0xFD2940
// Signature: sub rsp, 28h; cmp dword ptr [rcx+0B1Ch], 1
// Checks entity[711] to validate entity type
inline void* (*v_ZiplineEntityValidation)(_DWORD* a1);

// Caller function pointer - Zipline entity lookup at RVA 0xFCCE10
// This function sets rcx = 0 when entity handle is invalid, then calls validation
inline void* (*v_ZiplineEntityLookup)(__int64 a1, __int64 a2, __int64 a3, _DWORD* a4);

///////////////////////////////////////////////////////////////////////////////
class VZiplineFix : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("ZiplineEntityValidation", v_ZiplineEntityValidation);
		LogFunAdr("ZiplineEntityLookup", v_ZiplineEntityLookup);
	}
	virtual void GetFun(void) const
	{
		// sub_7FF7CEBC2940 - Entity validation function (RVA: 0xFD2940)
		// Prologue: sub rsp, 28h
		// At +0x4: cmp dword ptr [rcx+0B1Ch], 1
		// Bytes: 48 83 EC 28 83 B9 1C 0B 00 00 01
		Module_FindPattern(g_GameDll, "48 83 EC 28 83 B9 1C 0B 00 00 01")
			.GetPtr(v_ZiplineEntityValidation);

		// sub_7FF7CEBBCE10 - Zipline entity lookup function (RVA: 0xFCCE10)
		// Prologue:
		// mov [rsp+8], rbx
		// push rdi
		// sub rsp, 20h
		// cmp cs:byte_7FF7DB0DBF4D, 0
		// Bytes: 48 89 5C 24 08 57 48 83 EC 20 80 3D
		Module_FindPattern(g_GameDll, "48 89 5C 24 08 57 48 83 EC 20 80 3D")
			.GetPtr(v_ZiplineEntityLookup);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////
