//=============================================================================//
//
// Purpose: Prevent fatal error when visible object limit is reached.
//
// See clientleafsystem.h for design overview.
//
//=============================================================================//

#include "core/stdafx.h"
#include "game/client/clientleafsystem.h"

//-----------------------------------------------------------------------------
// Resolved addresses
//-----------------------------------------------------------------------------
static CMemory s_allocFunc;     // CClientLeafSystem::AllocVisibleObject (function start)
static CMemory s_freeAnchor;    // Unique body pattern in CClientLeafSystem::FreeVisibleObject
static uintptr_t s_clientLeafBase = 0; // g_ClientLeafSystem base address

//-----------------------------------------------------------------------------
// Binary patch infrastructure (same pattern as blast_pattern.cpp)
//-----------------------------------------------------------------------------
struct BytePatchRecord
{
	uint8_t* pAddr;
	uint8_t  size;
	uint32_t origValue;
};

static constexpr int MAX_PATCHES = 16;
static BytePatchRecord s_patches[MAX_PATCHES];
static int s_numPatches = 0;

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
// Runtime state for hooks
//-----------------------------------------------------------------------------
static std::atomic<int> s_visibleObjectCount{ 0 };
static std::atomic<bool> s_budgetWarned{ false };
static std::atomic<bool> s_overflowWarned{ false };
static bool s_overflowReserved = false;

//-----------------------------------------------------------------------------
// Public accessors
//-----------------------------------------------------------------------------
int ClientLeafSystem_GetVisibleObjectCount()
{
	return s_visibleObjectCount.load(std::memory_order_relaxed);
}

int ClientLeafSystem_GetVisibleObjectBudget()
{
	return VISIBLE_OBJECTS_BUDGET;
}

int ClientLeafSystem_GetVisibleObjectMax()
{
	return VISIBLE_OBJECTS_MAX - 1; // 8191 usable
}

bool ClientLeafSystem_IsOverflowing()
{
	return s_overflowWarned.load(std::memory_order_relaxed);
}

//-----------------------------------------------------------------------------
// Console command: cl_visible_objects
//-----------------------------------------------------------------------------
static void CC_VisibleObjects_f(const CCommand& args)
{
	const int count = s_visibleObjectCount.load(std::memory_order_relaxed);
	const int max = VISIBLE_OBJECTS_MAX - 1; // 8191 usable (8191 is overflow)
	const float pct = max > 0 ? (count * 100.0f / max) : 0.0f;

	Msg(eDLL_T::ENGINE,
		"Visible objects: %d / %d (%.1f%%) | budget: %d | overflow: %s\n",
		count, max, pct, VISIBLE_OBJECTS_BUDGET,
		s_overflowWarned.load() ? "YES" : "no");
}

static ConCommand cl_visible_objects(
	"cl_visible_objects",
	CC_VisibleObjects_f,
	"Print the current visible object count and budget status.",
	FCVAR_CLIENTDLL);

//-----------------------------------------------------------------------------
// Hook: AllocVisibleObject
//
// Called each time the engine needs a visible object handle. We intercept to:
// - Reserve the overflow handle on first invocation
// - Track allocation count for budget warnings
// - Return the overflow handle when the allocator is full (instead of crash)
//-----------------------------------------------------------------------------
unsigned short h_CClientLeafSystem_AllocVisibleObject()
{
	// On first call, permanently reserve the overflow handle by setting its
	// bit in IsInUse. This prevents the normal allocator from ever handing
	// out handle 8191, keeping it available as our overflow sentinel.
	if (!s_overflowReserved)
	{
		s_overflowReserved = true;

		if (s_clientLeafBase)
		{
			volatile int64_t* pIsInUse = reinterpret_cast<volatile int64_t*>(
				s_clientLeafBase + CLIENTLEAF_ISINUSE_OFFSET);
			_InterlockedOr64(const_cast<int64_t*>(&pIsInUse[OVERFLOW_CHUNK]),
				1LL << OVERFLOW_BIT);
		}
	}

	const unsigned short result = v_CClientLeafSystem_AllocVisibleObject();

	if (result != VISIBLE_OBJECTS_OVERFLOW_HANDLE)
	{
		// Normal allocation succeeded - track count and check budget.
		const int count = s_visibleObjectCount.fetch_add(1, std::memory_order_relaxed) + 1;

		if (count > VISIBLE_OBJECTS_BUDGET && !s_budgetWarned.exchange(true))
		{
			Warning(eDLL_T::ENGINE,
				"CClientLeafSystem: Over budget with %d visible objects "
				"(budget: %d, max: %d)\n",
				count, VISIBLE_OBJECTS_BUDGET, VISIBLE_OBJECTS_MAX - 1);
		}

		return result;
	}

	// Allocation failed (all 8191 real slots full) - patched allocator
	// returned the overflow handle instead of crashing. Warn once.
	if (!s_overflowWarned.exchange(true))
	{
		Warning(eDLL_T::ENGINE,
			"CClientLeafSystem: Visible object limit reached (%d). "
			"Overflow objects will share rendering data.\n",
			VISIBLE_OBJECTS_MAX - 1);
	}

	return VISIBLE_OBJECTS_OVERFLOW_HANDLE;
}

//-----------------------------------------------------------------------------
// Hook: FreeVisibleObject
//
// Protects the overflow handle from being freed and tracks count.
//-----------------------------------------------------------------------------
void h_CClientLeafSystem_FreeVisibleObject(__int64 a1, unsigned short handle)
{
	if (handle == VISIBLE_OBJECTS_OVERFLOW_HANDLE)
		return; // Never free the reserved overflow handle.

	s_visibleObjectCount.fetch_sub(1, std::memory_order_relaxed);
	v_CClientLeafSystem_FreeVisibleObject(a1, handle);
}

//-----------------------------------------------------------------------------
// IDetour: log resolved addresses
//-----------------------------------------------------------------------------
void VClientLeafSystem::GetAdr(void) const
{
	LogFunAdr("CClientLeafSystem::AllocVisibleObject",
		v_CClientLeafSystem_AllocVisibleObject);
	LogFunAdr("CClientLeafSystem::FreeVisibleObject",
		v_CClientLeafSystem_FreeVisibleObject);
	LogVarAdr("g_ClientLeafSystem", reinterpret_cast<void*>(s_clientLeafBase));
}

//-----------------------------------------------------------------------------
// IDetour: find functions via pattern scan
//-----------------------------------------------------------------------------
void VClientLeafSystem::GetFun(void) const
{
	//
	// AllocVisibleObject: use the unique 13-byte prologue.
	// This pattern has been verified to appear exactly once in the binary.
	//
	//   48 89 5C 24 18   mov [rsp+18h], rbx
	//   56               push rsi
	//   48 83 EC 30      sub rsp, 30h
	//   48 8D 0D         lea rcx, <critsec>   (start of next instruction)
	//
	s_allocFunc = Module_FindPattern(g_GameDll,
		"48 89 5C 24 18 56 48 83 EC 30 48 8D 0D");

	if (!s_allocFunc)
	{
		Warning(eDLL_T::ENGINE,
			"VClientLeafSystem: Failed to find AllocVisibleObject\n");
		return;
	}

	// The pattern IS the function start — no offset needed.
	v_CClientLeafSystem_AllocVisibleObject = s_allocFunc
		.RCast<unsigned short(*)()>();

	//
	// FreeVisibleObject: anchor on a unique body sequence at offset +0x1D
	// from the function start. This 14-byte pattern appears exactly once.
	//
	//   B9 FF FF 00 00   mov ecx, 0FFFFh
	//   8B D3            mov edx, ebx
	//   8B C3            mov eax, ebx
	//   48 03 C0         add rax, rax
	//   83 E2 3F         and edx, 3Fh
	//
	s_freeAnchor = Module_FindPattern(g_GameDll,
		"B9 FF FF 00 00 8B D3 8B C3 48 03 C0 83 E2 3F");

	if (!s_freeAnchor)
	{
		Warning(eDLL_T::ENGINE,
			"VClientLeafSystem: Failed to find FreeVisibleObject\n");
		return;
	}

	// The function starts 0x1D bytes before the anchor:
	//   +0x00: push rbx                   (2 bytes: 40 53)
	//   +0x02: sub rsp, 20h               (4 bytes)
	//   +0x06: lea rcx, <critsec>          (7 bytes)
	//   +0x0D: movzx ebx, dx              (3 bytes)
	//   +0x10: call [EnterCriticalSection]  (6 bytes)
	//   +0x16: lea r10, <clientleaf_base>  (7 bytes)
	//   +0x1D: mov ecx, 0FFFFh            <-- anchor
	//
	v_CClientLeafSystem_FreeVisibleObject = s_freeAnchor.Offset(-0x1D)
		.RCast<void(*)(__int64, unsigned short)>();
}

//-----------------------------------------------------------------------------
// IDetour: resolve variable addresses
//-----------------------------------------------------------------------------
void VClientLeafSystem::GetVar(void) const
{
	if (!s_freeAnchor)
		return;

	//
	// Resolve g_ClientLeafSystem base from the LEA r10 in FreeVisibleObject:
	//   4C 8D 15 ?? ?? ?? ??   at freeAnchor - 0x07   (func + 0x16)
	//
	s_clientLeafBase = s_freeAnchor.Offset(-0x07)
		.ResolveRelativeAddress(0x3, 0x7).GetPtr();
}

//-----------------------------------------------------------------------------
// IDetour: apply patches and set up hooks
//-----------------------------------------------------------------------------
void VClientLeafSystem::Detour(const bool bAttach) const
{
	if (!s_allocFunc || !s_freeAnchor ||
		!v_CClientLeafSystem_AllocVisibleObject ||
		!v_CClientLeafSystem_FreeVisibleObject)
	{
		Warning(eDLL_T::ENGINE,
			"VClientLeafSystem: Missing addresses, visible object "
			"limit patch not applied\n");
		return;
	}

	if (bAttach)
	{
		s_numPatches = 0;

		//
		// The allocator function (s_allocFunc = function start):
		//
		// When all bitfield chunks are exhausted, the error path begins
		// at func + 0x5F:
		//
		//   +0x5F: mov [rsp+40h], rbp        ; save rbp (5 bytes)
		//   +0x64: xor esi, esi               ; zero esi (2 bytes)
		//   +0x66: mov [rsp+48h], rdi         ; save rdi (5 bytes)
		//   +0x6B: lea rbp, <renderables_end> ; 7 bytes -- LOOP SETUP
		//   ... renderable iteration loop + fatal error call ...
		//   +0xDA: mov rdi, [rsp+48h]         ; restore rdi
		//   +0xDF: mov ebx, 0FFFFh            ; return invalid handle
		//   +0xE4: mov rbp, [rsp+40h]         ; restore rbp
		//   ... LeaveCriticalSection, return ...
		//
		// Patch 1: At +0x6B, replace the 7-byte LEA with a short jump
		// to +0xDA (restore rdi), skipping the renderable iteration loop
		// and fatal error call entirely.
		//
		// jmp displacement: from (+0x6B+2) to (+0xDA) = 0x6D
		//
		uint8_t* pJmpPatch = s_allocFunc.Offset(0x6B).RCast<uint8_t*>();

		// Overwrite the 7-byte LEA with: EB 6D (jmp short +0x6D) + 5x NOP
		ApplyPatch(pJmpPatch,     0xEB, 1); // jmp short opcode
		ApplyPatch(pJmpPatch + 1, 0x6D, 1); // displacement to +0xDA

		for (int i = 2; i < 7; i++)
			ApplyPatch(pJmpPatch + i, 0x90, 1); // NOP remaining bytes

		//
		// Patch 2: Change the return value from 0xFFFF to the overflow handle.
		// "mov ebx, 0FFFFh" at +0xDF is encoded as: BB FF FF 00 00
		// Byte at +0xE1 is the high byte of the 16-bit value (0xFF -> 0x1F).
		//
		// 0x0000FFFF -> 0x00001FFF  (little-endian: FF FF -> FF 1F)
		//
		static_assert(VISIBLE_OBJECTS_OVERFLOW_HANDLE == 0x1FFF,
			"Overflow handle encoding assumption");

		uint8_t* pRetValPatch = s_allocFunc.Offset(0xE1).RCast<uint8_t*>();
		ApplyPatch(pRetValPatch, 0x1F, 1);

		DevMsg(eDLL_T::ENGINE,
			"Patched CClientLeafSystem: overflow handle=%d, budget=%d "
			"(fatal error disabled)\n",
			VISIBLE_OBJECTS_OVERFLOW_HANDLE, VISIBLE_OBJECTS_BUDGET);

		// Set up function detour hooks for budget warnings + overflow protection.
		DetourSetup(&v_CClientLeafSystem_AllocVisibleObject,
			&h_CClientLeafSystem_AllocVisibleObject, bAttach);
		DetourSetup(&v_CClientLeafSystem_FreeVisibleObject,
			&h_CClientLeafSystem_FreeVisibleObject, bAttach);
	}
	else // Detach
	{
		// Remove function hooks first.
		DetourSetup(&v_CClientLeafSystem_AllocVisibleObject,
			&h_CClientLeafSystem_AllocVisibleObject, bAttach);
		DetourSetup(&v_CClientLeafSystem_FreeVisibleObject,
			&h_CClientLeafSystem_FreeVisibleObject, bAttach);

		// Restore binary patches.
		RestoreAllPatches();

		// Reset runtime state.
		s_overflowReserved = false;
		s_budgetWarned.store(false);
		s_overflowWarned.store(false);
		s_visibleObjectCount.store(0);
	}
}
