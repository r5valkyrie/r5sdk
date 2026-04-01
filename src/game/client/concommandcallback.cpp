//=============================================================================//
//
// Purpose: Expand the ConCommand script callback pool.
//
// See concommandcallback.h for design overview.
//
//=============================================================================//

#include "core/stdafx.h"
#include "game/client/concommandcallback.h"

//-----------------------------------------------------------------------------
// Resolved addresses
//-----------------------------------------------------------------------------
static CMemory s_registerFunc;  // RegisterConCommandTriggeredCallback (function start)
static uintptr_t* s_pFreeListHead = nullptr; // s_conCommandScriptCallbackFreeListHead

//-----------------------------------------------------------------------------
// Track dynamically allocated blocks so we can free them on detach.
//-----------------------------------------------------------------------------
static constexpr int MAX_GROW_BLOCKS = 32;
static void* s_growBlocks[MAX_GROW_BLOCKS];
static int s_numGrowBlocks = 0;

//-----------------------------------------------------------------------------
// Purpose: Allocate a new batch of callback nodes and prepend them to the
//          free list. Each node is 40 bytes with a doubly-linked list at
//          offsets 0x18 (prev) and 0x20 (next).
//-----------------------------------------------------------------------------
static void GrowConCommandCallbackPool()
{
	if (s_numGrowBlocks >= MAX_GROW_BLOCKS)
	{
		Warning(eDLL_T::ENGINE,
			"ConCommandCallback: Exceeded maximum grow block count (%d)\n",
			MAX_GROW_BLOCKS);
		return;
	}

	const size_t blockSize =
		static_cast<size_t>(CONCOMMAND_CALLBACK_GROW_COUNT) * CONCOMMAND_CALLBACK_NODE_SIZE;

	uint8_t* pBlock = static_cast<uint8_t*>(
		VirtualAlloc(nullptr, blockSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

	if (!pBlock)
	{
		Warning(eDLL_T::ENGINE,
			"ConCommandCallback: VirtualAlloc failed for %zu bytes\n", blockSize);
		return;
	}

	s_growBlocks[s_numGrowBlocks++] = pBlock;
	memset(pBlock, 0, blockSize);

	// Build the free list from the new block.
	// Prepend each node to the existing free list head.
	uintptr_t head = *s_pFreeListHead;

	for (int i = 0; i < CONCOMMAND_CALLBACK_GROW_COUNT; i++)
	{
		uint8_t* pNode = pBlock + (i * CONCOMMAND_CALLBACK_NODE_SIZE);

		// node->prev = NULL (already zeroed)
		// node->next = current head
		*reinterpret_cast<uintptr_t*>(pNode + 0x20) = head;

		// old head->prev = new node
		if (head)
			*reinterpret_cast<uintptr_t*>(head + 0x18) = reinterpret_cast<uintptr_t>(pNode);

		head = reinterpret_cast<uintptr_t>(pNode);
	}

	*s_pFreeListHead = head;

	DevMsg(eDLL_T::ENGINE,
		"ConCommandCallback: Grew pool by %d entries (block %d)\n",
		CONCOMMAND_CALLBACK_GROW_COUNT, s_numGrowBlocks);
}

//-----------------------------------------------------------------------------
// Hook: RegisterConCommandTriggeredCallback
//
// Before the original function executes, check if the free list is exhausted.
// If so, grow the pool dynamically to prevent the "Can not register more
// than 50 ConCommand callbacks" script error.
//-----------------------------------------------------------------------------
__int64 h_RegisterConCommandTriggeredCallback(__int64 sqvm)
{
	if (s_pFreeListHead && !*s_pFreeListHead)
		GrowConCommandCallbackPool();

	return v_RegisterConCommandTriggeredCallback(sqvm);
}

//-----------------------------------------------------------------------------
// IDetour: log resolved addresses
//-----------------------------------------------------------------------------
void VConCommandCallback::GetAdr(void) const
{
	LogFunAdr("RegisterConCommandTriggeredCallback",
		v_RegisterConCommandTriggeredCallback);
	LogVarAdr("s_conCommandScriptCallbackFreeListHead",
		reinterpret_cast<void*>(s_pFreeListHead));
}

//-----------------------------------------------------------------------------
// IDetour: find functions via pattern scan
//-----------------------------------------------------------------------------
void VConCommandCallback::GetFun(void) const
{
	//
	// RegisterConCommandTriggeredCallback: unique 20-byte prologue.
	// Verified to appear exactly once in the binary.
	//
	//   40 55               push rbp
	//   48 83 EC 70         sub rsp, 70h
	//   48 8B 51 58         mov rdx, [rcx+58h]
	//   48 8B E9            mov rbp, rcx
	//   81 7A 10 10 00 00 08  cmp dword ptr [rdx+10h], 8000010h
	//
	s_registerFunc = Module_FindPattern(g_GameDll,
		"40 55 48 83 EC 70 48 8B 51 58 48 8B E9 81 7A 10 10 00 00 08");

	if (!s_registerFunc)
	{
		Warning(eDLL_T::ENGINE,
			"VConCommandCallback: Failed to find "
			"RegisterConCommandTriggeredCallback\n");
		return;
	}

	v_RegisterConCommandTriggeredCallback = s_registerFunc
		.RCast<__int64(*)(__int64)>();
}

//-----------------------------------------------------------------------------
// IDetour: resolve variable addresses
//-----------------------------------------------------------------------------
void VConCommandCallback::GetVar(void) const
{
	if (!s_registerFunc)
		return;

	//
	// At func+0x155 there is:
	//   48 8B 05 xx xx xx xx   mov rax, cs:s_conCommandScriptCallbackFreeListHead
	//
	// Resolve the RIP-relative address to get the free list head pointer.
	//
	s_pFreeListHead = s_registerFunc.Offset(0x155)
		.ResolveRelativeAddress(0x3, 0x7)
		.RCast<uintptr_t*>();
}

//-----------------------------------------------------------------------------
// IDetour: set up hooks
//-----------------------------------------------------------------------------
void VConCommandCallback::Detour(const bool bAttach) const
{
	if (!s_registerFunc || !v_RegisterConCommandTriggeredCallback ||
		!s_pFreeListHead)
	{
		Warning(eDLL_T::ENGINE,
			"VConCommandCallback: Missing addresses, ConCommand callback "
			"pool expansion not available\n");
		return;
	}

	DetourSetup(&v_RegisterConCommandTriggeredCallback,
		&h_RegisterConCommandTriggeredCallback, bAttach);

	if (bAttach)
	{
		DevMsg(eDLL_T::ENGINE,
			"ConCommandCallback: Pool expansion enabled "
			"(grow by %d per block)\n",
			CONCOMMAND_CALLBACK_GROW_COUNT);
	}
	else
	{
		// Free all dynamically allocated blocks.
		for (int i = 0; i < s_numGrowBlocks; i++)
		{
			if (s_growBlocks[i])
			{
				VirtualFree(s_growBlocks[i], 0, MEM_RELEASE);
				s_growBlocks[i] = nullptr;
			}
		}
		s_numGrowBlocks = 0;
	}
}
