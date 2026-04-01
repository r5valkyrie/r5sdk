//===========================================================================//
//
// Purpose: Fix for zipline entity validation crash on S17+ converted maps
//
// Fix for: EXCEPTION_ACCESS_VIOLATION(read): 0x0B1C at r5apex.exe+0xFD2944
//
// Root cause: Entity validation function sub_7FF7CEBC2940 is called with
// NULL pointer from sub_7FF7CEBBCE10 when zipline entity handles are invalid.
// The validation function accesses a1[711] (offset 0xB1C) without NULL check.
//
// This happens when:
// 1. Loading maps converted from S17+ with zipline entity references
// 2. Zipline entities aren't properly initialized or don't exist
// 3. Entity handle lookup fails, returns NULL
// 4. NULL passed to validation -> crash at offset 0xB1C
//
//===========================================================================//

#include "core/stdafx.h"
#include "game/client/c_zipline_fix.h"

//-----------------------------------------------------------------------------
// Purpose: Hooked entity validation function with NULL pointer protection
// Input  : a1 - Entity pointer (can be NULL!)
// Output : Validation result (nullptr if NULL or invalid)
//-----------------------------------------------------------------------------
static void* ZiplineEntityValidation_Hook(_DWORD* a1)
{
	// NULL check to prevent crash at a1[711]
	if (!a1)
	{
		// Log once to avoid spam
		static bool s_bWarned = false;
		if (!s_bWarned)
		{
			Warning(eDLL_T::CLIENT,
				"ZiplineEntityValidation: NULL entity pointer passed, "
				"skipping validation to prevent crash\n");
			s_bWarned = true;
		}
		return nullptr;  // Return NULL (invalid entity)
	}

	// Valid pointer, call original function
	return v_ZiplineEntityValidation(a1);
}

///////////////////////////////////////////////////////////////////////////////
void VZiplineFix::Detour(const bool bAttach) const
{
	// Only hook if signature was found
	if (v_ZiplineEntityValidation)
	{
		DetourSetup(&v_ZiplineEntityValidation, &ZiplineEntityValidation_Hook, bAttach);
	}
}
