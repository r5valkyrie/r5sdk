//=============================================================================//
//
// Purpose: Increase blast pattern and projectile limits.
//
// The engine stores blast patterns in a fixed-size static array (8 entries,
// each 292 bytes supporting 16 bullets). Other global variables sit right
// after the array, so we must relocate it to a new allocation.
//
// This module:
// 1. Allocates a larger array (64 entries x 1060 bytes = 64 bullets each)
// 2. Patches all LEA instructions referencing the old array
// 3. Patches all IMUL instructions using the old entry stride (0x124 -> 0x424)
// 4. Patches the pattern count limit (8 -> 64) and bullet count limit (16 -> 64)
// 5. Patches the corresponding error message parameters
// 6. Patches the projectiles_per_shot limit (12 -> 64) in both client/server
//
//=============================================================================//

#include "core/stdafx.h"
#include "game/shared/r1/blast_pattern.h"

//-----------------------------------------------------------------------------
// Memory patch locations (resolved in GetFun/GetVar)
//-----------------------------------------------------------------------------
static CMemory s_LoadFunc;                    // LoadWeaponBlastPatterns_Client
static uintptr_t s_pOldArray = 0;            // Original blast pattern array address

static uint8_t* s_pPatternCmpImmed = nullptr; // Immediate byte in "cmp ecx, 7" (pattern count limit)
static uint8_t* s_pPatternMovImmed = nullptr; // Immediate dword in "mov r8d, 8" (pattern count error)
static uint8_t* s_pBulletCmpImmed = nullptr;  // Immediate byte in "cmp rbp, 10h" (bullet count limit)
static uint8_t* s_pBulletMovImmed = nullptr;  // Immediate dword in "mov r8d, 10h" (bullet count error)

//-----------------------------------------------------------------------------
// Runtime state
//-----------------------------------------------------------------------------
static void* s_pNewArray = nullptr;           // VirtualAlloc'd expanded array

struct BytePatchRecord
{
	uint8_t* pAddr;      // Address of patched byte(s)
	uint8_t  size;       // Number of bytes (1 or 4)
	uint32_t origValue;  // Original value for restoration
};

static constexpr int MAX_PATCHES = 64;
static BytePatchRecord s_patches[MAX_PATCHES];
static int s_numPatches = 0;

//-----------------------------------------------------------------------------
// Helper: record and apply a patch
//-----------------------------------------------------------------------------
static void ApplyPatch(uint8_t* pAddr, uint32_t newValue, uint8_t size)
{
	if (s_numPatches >= MAX_PATCHES)
		return;

	BytePatchRecord& rec = s_patches[s_numPatches++];
	rec.pAddr = pAddr;
	rec.size = size;

	DWORD oldProtect;
	VirtualProtect(pAddr, size, PAGE_EXECUTE_READWRITE, &oldProtect);

	if (size == 1)
	{
		rec.origValue = *pAddr;
		*pAddr = static_cast<uint8_t>(newValue);
	}
	else
	{
		rec.origValue = *reinterpret_cast<uint32_t*>(pAddr);
		*reinterpret_cast<uint32_t*>(pAddr) = newValue;
	}

	VirtualProtect(pAddr, size, oldProtect, &oldProtect);
}

//-----------------------------------------------------------------------------
// Helper: restore all recorded patches
//-----------------------------------------------------------------------------
static void RestoreAllPatches()
{
	for (int i = 0; i < s_numPatches; i++)
	{
		BytePatchRecord& rec = s_patches[i];
		DWORD oldProtect;
		VirtualProtect(rec.pAddr, rec.size, PAGE_EXECUTE_READWRITE, &oldProtect);

		if (rec.size == 1)
			*rec.pAddr = static_cast<uint8_t>(rec.origValue);
		else
			*reinterpret_cast<uint32_t*>(rec.pAddr) = rec.origValue;

		VirtualProtect(rec.pAddr, rec.size, oldProtect, &oldProtect);
	}
	s_numPatches = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Log resolved addresses for debugging
//-----------------------------------------------------------------------------
void VBlastPattern::GetAdr(void) const
{
	LogFunAdr("LoadWeaponBlastPatterns_Client (anchor)", s_LoadFunc.RCast<void*>());
	LogVarAdr("g_blastPatterns", reinterpret_cast<void*>(s_pOldArray));
}

//-----------------------------------------------------------------------------
// Purpose: Find the load function via pattern scan
//-----------------------------------------------------------------------------
void VBlastPattern::GetFun(void) const
{
	// NOTE: The function prologue "40 53 41 55 48 81 EC 68 01 00 00" is shared
	// with LoadWeaponViewkickPatterns_Client. We anchor on a unique sequence
	// inside the blast patterns loader instead:
	//
	//   83 F9 07                 cmp ecx, 7
	//   0F 84 ?? ?? ?? ??       jz <error>
	//   4C 69 F9 24 01 00 00    imul r15, rcx, 124h
	//
	// This pattern uniquely identifies the blast patterns limit check
	// (viewkick uses cmp ecx, 1Fh and imul with stride 42Ch).
	s_LoadFunc = Module_FindPattern(g_GameDll, "83 F9 07 0F 84 ?? ?? ?? ?? 4C 69 F9 24 01 00 00");
}

//-----------------------------------------------------------------------------
// Purpose: Resolve variable addresses from the found function
//-----------------------------------------------------------------------------
void VBlastPattern::GetVar(void) const
{
	if (!s_LoadFunc)
	{
		Warning(eDLL_T::ENGINE, "VBlastPattern: LoadWeaponBlastPatterns_Client not found\n");
		return;
	}

	//
	// s_LoadFunc points to the "cmp ecx, 7" instruction found in GetFun.
	// The CMP immediate is at offset 2 within the instruction (83 F9 [07]).
	//
	s_pPatternCmpImmed = s_LoadFunc.Offset(0x2).RCast<uint8_t*>();

	//
	// The LEA to the blast pattern array is the 7-byte instruction
	// immediately before the cmp:
	//   48 8D 15 ?? ?? ?? ??   lea rdx, g_blastPatterns
	//
	s_pOldArray = s_LoadFunc.Offset(-7).ResolveRelativeAddress(0x3, 0x7).GetPtr();

	//
	// Pattern count error: "mov r8d, 8" followed by "lea rdx" for the error string.
	// Search forward from the cmp within the same function.
	//
	CMemory patternMov = s_LoadFunc.FindPattern(
		"41 B8 08 00 00 00 48 8D 15", CMemory::Direction::DOWN, 512);
	if (patternMov)
		s_pPatternMovImmed = patternMov.Offset(0x2).RCast<uint8_t*>();
	else
		Warning(eDLL_T::ENGINE, "VBlastPattern: Failed to find pattern count error mov\n");

	//
	// Bullet count: find "cmp rbp, 10h" (48 83 FD 10) for max bullets check.
	//
	CMemory bulletCmp = s_LoadFunc.FindPattern("48 83 FD 10", CMemory::Direction::DOWN, 512);
	if (bulletCmp)
		s_pBulletCmpImmed = bulletCmp.Offset(0x3).RCast<uint8_t*>();
	else
		Warning(eDLL_T::ENGINE, "VBlastPattern: Failed to find bullet count cmp\n");

	//
	// Bullet count error: "mov r8d, 10h" followed by "lea rcx" for the bullet error string.
	//
	CMemory bulletMov = s_LoadFunc.FindPattern(
		"41 B8 10 00 00 00 48 8D 0D", CMemory::Direction::DOWN, 512);
	if (bulletMov)
		s_pBulletMovImmed = bulletMov.Offset(0x2).RCast<uint8_t*>();
	else
		Warning(eDLL_T::ENGINE, "VBlastPattern: Failed to find bullet count error mov\n");
}

//-----------------------------------------------------------------------------
// Purpose: Allocate new array and apply all patches
//-----------------------------------------------------------------------------
void VBlastPattern::Detour(const bool bAttach) const
{
	if (!s_pOldArray || !s_pPatternCmpImmed || !s_pPatternMovImmed)
	{
		Warning(eDLL_T::ENGINE, "VBlastPattern: Missing addresses, blast pattern limits not patched\n");
		return;
	}

	if (bAttach)
	{
		const size_t nNewArraySize = static_cast<size_t>(BLAST_PATTERN_MAX_NEW) * BLAST_PATTERN_ENTRY_SIZE_NEW;

		//
		// Allocate the expanded array near the game module so that all
		// RIP-relative displacements fit within a signed 32-bit range.
		//
		const uintptr_t moduleEnd = g_GameDll.GetModuleBase() + g_GameDll.GetModuleSize();

		for (uintptr_t addr = (moduleEnd + 0xFFFF) & ~(uintptr_t)0xFFFF;
			addr < moduleEnd + 0x40000000;
			addr += 0x10000)
		{
			s_pNewArray = VirtualAlloc(
				reinterpret_cast<void*>(addr),
				nNewArraySize,
				MEM_COMMIT | MEM_RESERVE,
				PAGE_READWRITE);

			if (s_pNewArray)
				break;
		}

		if (!s_pNewArray)
		{
			Warning(eDLL_T::ENGINE, "VBlastPattern: Failed to allocate expanded array\n");
			return;
		}

		memset(s_pNewArray, 0, nNewArraySize);

		s_numPatches = 0;

		const CModule::ModuleSections_t& textSection = g_GameDll.GetSectionByName(".text");
		uint8_t* pText = reinterpret_cast<uint8_t*>(textSection.m_pSectionBase);
		const size_t textSize = textSection.m_nSectionSize;

		int numLeaPatched = 0;
		int numImulPatched = 0;

		for (size_t i = 0; i + 7 <= textSize; i++)
		{
			const uint8_t b0 = pText[i];

			//
			// Check for LEA with RIP-relative addressing to old array:
			//   [48|4C] 8D [ModRM] [disp32]   (7 bytes)
			//   ModRM & 0xC7 == 0x05 means [RIP+disp32] mode
			//
			if ((b0 == 0x48 || b0 == 0x4C) && pText[i + 1] == 0x8D &&
				(pText[i + 2] & 0xC7) == 0x05)
			{
				const int32_t disp = *reinterpret_cast<int32_t*>(&pText[i + 3]);
				const uintptr_t nextIP = reinterpret_cast<uintptr_t>(&pText[i + 7]);
				const uintptr_t target = nextIP + disp;

				if (target == s_pOldArray)
				{
					const int64_t newDisp64 =
						static_cast<int64_t>(reinterpret_cast<uintptr_t>(s_pNewArray)) -
						static_cast<int64_t>(nextIP);

					if (newDisp64 >= INT32_MIN && newDisp64 <= INT32_MAX)
					{
						ApplyPatch(&pText[i + 3], static_cast<uint32_t>(static_cast<int32_t>(newDisp64)), 4);
						numLeaPatched++;
					}
				}
			}

			//
			// Check for IMUL with old entry stride (0x124):
			//   [48|4C] 69 [reg] 24 01 00 00   (7 bytes)
			//   Patch immediate from 0x124 to 0x224
			//
			if ((b0 == 0x48 || b0 == 0x4C) && pText[i + 1] == 0x69 &&
				*reinterpret_cast<uint32_t*>(&pText[i + 3]) == BLAST_PATTERN_ENTRY_SIZE_OLD)
			{
				ApplyPatch(&pText[i + 3], static_cast<uint32_t>(BLAST_PATTERN_ENTRY_SIZE_NEW), 4);
				numImulPatched++;
			}
		}

		//
		// Patch pattern count limit: cmp ecx, 7 -> cmp ecx, 31
		//
		ApplyPatch(s_pPatternCmpImmed, BLAST_PATTERN_MAX_NEW - 1, 1);

		//
		// Patch pattern count error: mov r8d, 8 -> mov r8d, 32
		//
		ApplyPatch(s_pPatternMovImmed, BLAST_PATTERN_MAX_NEW, 4);

		//
		// Patch bullet count limit: cmp rbp, 10h -> cmp rbp, 20h
		//
		if (s_pBulletCmpImmed)
			ApplyPatch(s_pBulletCmpImmed, BLAST_PATTERN_BULLETS_MAX_NEW, 1);

		//
		// Patch bullet count error: mov r8d, 10h -> mov r8d, 20h
		//
		if (s_pBulletMovImmed)
			ApplyPatch(s_pBulletMovImmed, BLAST_PATTERN_BULLETS_MAX_NEW, 4);

		//
		// Patch projectiles_per_shot limit (12 -> 64) in both client and server
		// weapon parse functions. The sequence is:
		//   45 8B 86 D0 02 00 00  mov r8d, [r14+2D0h]
		//   41 83 F8 0C            cmp r8d, 0Ch          ; offset +10: immediate
		//   7E 16                  jle skip
		//   41 B9 0C 00 00 00      mov r9d, 0Ch          ; offset +15: immediate
		//
		int numProjPatched = 0;

		for (size_t i = 0; i + 19 <= textSize; i++)
		{
			if (pText[i]     == 0x45 && pText[i + 1] == 0x8B &&
				pText[i + 2] == 0x86 && pText[i + 3] == 0xD0 &&
				pText[i + 4] == 0x02 && pText[i + 5] == 0x00 &&
				pText[i + 6] == 0x00 && pText[i + 7] == 0x41 &&
				pText[i + 8] == 0x83 && pText[i + 9] == 0xF8 &&
				pText[i + 10] == PROJECTILES_PER_SHOT_MAX_OLD)
			{
				// Patch cmp r8d, 0Ch -> cmp r8d, <new>
				ApplyPatch(&pText[i + 10], PROJECTILES_PER_SHOT_MAX_NEW, 1);

				// Patch mov r9d, 0Ch -> mov r9d, <new> (at offset +15)
				if (pText[i + 13] == 0x41 && pText[i + 14] == 0xB9 &&
					*reinterpret_cast<uint32_t*>(&pText[i + 15]) == PROJECTILES_PER_SHOT_MAX_OLD)
				{
					ApplyPatch(&pText[i + 15], PROJECTILES_PER_SHOT_MAX_NEW, 4);
				}

				numProjPatched++;
			}
		}

		DevMsg(eDLL_T::ENGINE,
			"Patched blast patterns: max patterns %d->%d, max bullets %d->%d "
			"(%d LEA, %d IMUL relocations), projectiles_per_shot %d->%d (%d sites)\n",
			BLAST_PATTERN_MAX_OLD, BLAST_PATTERN_MAX_NEW,
			BLAST_PATTERN_BULLETS_MAX_OLD, BLAST_PATTERN_BULLETS_MAX_NEW,
			numLeaPatched, numImulPatched,
			PROJECTILES_PER_SHOT_MAX_OLD, PROJECTILES_PER_SHOT_MAX_NEW, numProjPatched);
	}
	else // Detach: restore everything
	{
		RestoreAllPatches();

		if (s_pNewArray)
		{
			VirtualFree(s_pNewArray, 0, MEM_RELEASE);
			s_pNewArray = nullptr;
		}
	}
}
