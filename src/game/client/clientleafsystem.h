//=============================================================================//
//
// Purpose: Prevent fatal error when visible object limit is reached.
//
// CClientLeafSystem uses a fixed-size bitfield allocator for visible object
// handles (128 chunks x 64 bits = 8192 max). When all handles are exhausted,
// the engine raises a fatal error and crashes.
//
// This module:
// 1. Patches the allocator's fatal error path to return gracefully
// 2. Reserves the last handle (8191) as an overflow handle
// 3. Overflow entities share handle 8191 (minor visual glitches, no crash)
// 4. Adds budget warnings when approaching the limit
//
//=============================================================================//

#ifndef GAME_CLIENTLEAFSYSTEM_H
#define GAME_CLIENTLEAFSYSTEM_H

#include "thirdparty/detours/include/idetour.h"

//-----------------------------------------------------------------------------
// Visible object limits
//-----------------------------------------------------------------------------
inline constexpr int VISIBLE_OBJECTS_CHUNK_COUNT = 128;
inline constexpr int VISIBLE_OBJECTS_MAX = VISIBLE_OBJECTS_CHUNK_COUNT * 64; // 8192

// Budget threshold - warn when exceeding this count.
inline constexpr int VISIBLE_OBJECTS_BUDGET = 7168;

// Overflow handle: the last valid index, reserved for entities that can't
// get a real handle. Multiple overflow entities share this slot.
inline constexpr unsigned short VISIBLE_OBJECTS_OVERFLOW_HANDLE =
	static_cast<unsigned short>(VISIBLE_OBJECTS_MAX - 1); // 8191

// Overflow handle's chunk and bit position in the IsInUse bitfield.
inline constexpr int OVERFLOW_CHUNK = VISIBLE_OBJECTS_OVERFLOW_HANDLE >> 6;  // 127
inline constexpr int OVERFLOW_BIT   = VISIBLE_OBJECTS_OVERFLOW_HANDLE & 63;  // 63

// IsInUse bitfield offset from struct base (array of uint64[128]).
inline constexpr int CLIENTLEAF_ISINUSE_OFFSET = 0x38;

//-----------------------------------------------------------------------------
// Original function pointers (resolved in GetFun)
//-----------------------------------------------------------------------------
inline unsigned short(*v_CClientLeafSystem_AllocVisibleObject)();
inline void(*v_CClientLeafSystem_FreeVisibleObject)(__int64 a1, unsigned short handle);

//-----------------------------------------------------------------------------
// Hook functions
//-----------------------------------------------------------------------------
unsigned short h_CClientLeafSystem_AllocVisibleObject();
void h_CClientLeafSystem_FreeVisibleObject(__int64 a1, unsigned short handle);

//-----------------------------------------------------------------------------
// Public accessors (for VScript bindings, etc.)
//-----------------------------------------------------------------------------
int  ClientLeafSystem_GetVisibleObjectCount();
int  ClientLeafSystem_GetVisibleObjectBudget();
int  ClientLeafSystem_GetVisibleObjectMax();
bool ClientLeafSystem_IsOverflowing();

///////////////////////////////////////////////////////////////////////////////
class VClientLeafSystem : public IDetour
{
	virtual void GetAdr(void) const;
	virtual void GetFun(void) const;
	virtual void GetVar(void) const;
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // GAME_CLIENTLEAFSYSTEM_H
