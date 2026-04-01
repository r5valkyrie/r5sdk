//=============================================================================//
//
// Purpose: Increase weapon blast pattern and projectile limits.
//
//=============================================================================//

#ifndef GAME_BLAST_PATTERN_H
#define GAME_BLAST_PATTERN_H

#include "thirdparty/detours/include/idetour.h"

//-----------------------------------------------------------------------------
// Blast pattern limits
//-----------------------------------------------------------------------------
constexpr int BLAST_PATTERN_MAX_OLD = 8;               // Original max pattern count
constexpr int BLAST_PATTERN_MAX_NEW = 128;              // New max pattern count

constexpr int BLAST_PATTERN_BULLETS_MAX_OLD = 16;      // Original max bullets per pattern
constexpr int BLAST_PATTERN_BULLETS_MAX_NEW = 64;      // New max bullets per pattern

constexpr int BLAST_PATTERN_BULLET_SIZE = 0x10;        // 16 bytes per bullet (4 floats)
constexpr int BLAST_PATTERN_HEADER_SIZE = 0x24;        // 36 bytes (name[32] + numBullets)

constexpr int BLAST_PATTERN_ENTRY_SIZE_OLD = BLAST_PATTERN_HEADER_SIZE + BLAST_PATTERN_BULLETS_MAX_OLD * BLAST_PATTERN_BULLET_SIZE; // 292 (0x124)
constexpr int BLAST_PATTERN_ENTRY_SIZE_NEW = BLAST_PATTERN_HEADER_SIZE + BLAST_PATTERN_BULLETS_MAX_NEW * BLAST_PATTERN_BULLET_SIZE; // 1060 (0x424)

//-----------------------------------------------------------------------------
// Projectiles per shot limits
//-----------------------------------------------------------------------------
constexpr int PROJECTILES_PER_SHOT_MAX_OLD = 12;       // Original max projectiles per shot
constexpr int PROJECTILES_PER_SHOT_MAX_NEW = 64;       // New max projectiles per shot

///////////////////////////////////////////////////////////////////////////////
class VBlastPattern : public IDetour
{
	virtual void GetAdr(void) const;
	virtual void GetFun(void) const;
	virtual void GetVar(void) const;
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // GAME_BLAST_PATTERN_H
