//=============================================================================
// staticprop_bounds_debug.cpp - Debug hook for static prop bounds check crash
//=============================================================================
#include "core/stdafx.h"
#include "tier0/commandline.h"
#include "tier1/cvar.h"
#include "engine/staticprop_bounds_debug.h"

// BSP version global for debugging
inline int* g_pBspVersion = nullptr;  // dword_7FF736C9C6C4

//-----------------------------------------------------------------------------
// Purpose: Hook for StaticPropBoundsCheck to catch the crash and log debug info
// 
// This function is called during visibility traversal with:
//   - staticPropIndex: The index into static prop arrays (already adjusted by threshold)
//   - position: Camera/query position (3 floats)
//   - radiusSq: Squared radius for distance check
//
// The crash happens when:
//   - g_pStaticPropData or g_pStaticPropBounds is NULL
//   - staticPropIndex is out of bounds
//-----------------------------------------------------------------------------
bool StaticPropBoundsCheck_Hook(unsigned int staticPropIndex, float* position, float radiusSq)
{
	// Get current values of globals for debugging
	void* propData = g_pStaticPropData ? *g_pStaticPropData : nullptr;
	void* propBounds = g_pStaticPropBounds ? *g_pStaticPropBounds : nullptr;
	int threshold = g_pStaticPropThreshold ? *g_pStaticPropThreshold : -1;
	int bspVersion = g_pBspVersion ? *g_pBspVersion : -1;
	
	// Log every call for debugging (will be very spammy, but useful for crash analysis)
	static int callCount = 0;
	static bool hasWarned = false;
	
	callCount++;
	
	// Check for problematic conditions
	bool isBad = false;
	
	if (!propData)
	{
		if (!hasWarned)
		{
			Warning(eDLL_T::ENGINE, "StaticPropBoundsCheck: g_pStaticPropData is NULL! index=%u threshold=%d bspVersion=%d\n", 
				staticPropIndex, threshold, bspVersion);
			hasWarned = true;
		}
		isBad = true;
	}
	
	if (!propBounds)
	{
		if (!hasWarned)
		{
			Warning(eDLL_T::ENGINE, "StaticPropBoundsCheck: g_pStaticPropBounds is NULL! index=%u threshold=%d bspVersion=%d\n", 
				staticPropIndex, threshold, bspVersion);
			hasWarned = true;
		}
		isBad = true;
	}
	
	// Log first few calls and any potentially problematic ones
	if (callCount <= 10 || isBad)
	{
		Msg(eDLL_T::ENGINE, "StaticPropBoundsCheck[%d]: index=%u, pos=(%.1f, %.1f, %.1f), radiusSq=%.1f, "
			"propData=%p, propBounds=%p, threshold=%d, bspVersion=%d\n",
			callCount, staticPropIndex, 
			position ? position[0] : 0.0f, 
			position ? position[1] : 0.0f, 
			position ? position[2] : 0.0f,
			radiusSq,
			propData, propBounds, threshold, bspVersion);
	}
	
	// If we detect a problem, skip the original function to prevent crash
	if (isBad)
	{
		Warning(eDLL_T::ENGINE, "StaticPropBoundsCheck: Skipping call to prevent crash!\n");
		return false;  // Indicate the prop is not visible
	}
	
	// Call the original function
	return v_StaticPropBoundsCheck(staticPropIndex, position, radiusSq);
}
