//=============================================================================//
//
// Purpose: Expand the ConCommand script callback pool beyond the engine's
//          hardcoded limit of 50 entries.
//
// The engine pre-allocates a fixed pool of ConCommandScriptCallback nodes
// used by RegisterConCommandTriggeredCallback (called from Squirrel scripts).
// When the pool is exhausted, the engine raises a script error:
//   "[CLIENT] Can not register more than 50 ConCommand callbacks"
//
// This module hooks the registration function and dynamically grows the
// free list when exhausted, eliminating the limit entirely.
//
//=============================================================================//

#ifndef GAME_CLIENT_CONCOMMANDCALLBACK_H
#define GAME_CLIENT_CONCOMMANDCALLBACK_H

#include "thirdparty/detours/include/idetour.h"

//-----------------------------------------------------------------------------
// ConCommandScriptCallback node layout (40 bytes / 0x28):
//   0x00: tagSQObject callback  (16 bytes)
//   0x10: ConCommand* command   (8 bytes)
//   0x18: prev pointer          (8 bytes)
//   0x20: next pointer          (8 bytes)
//-----------------------------------------------------------------------------
inline constexpr int CONCOMMAND_CALLBACK_NODE_SIZE = 0x28;  // 40 bytes

// How many extra nodes to allocate each time the pool runs out.
inline constexpr int CONCOMMAND_CALLBACK_GROW_COUNT = 50;

//-----------------------------------------------------------------------------
// Original function pointer
//-----------------------------------------------------------------------------
inline __int64(*v_RegisterConCommandTriggeredCallback)(__int64 sqvm);

//-----------------------------------------------------------------------------
// Hook
//-----------------------------------------------------------------------------
__int64 h_RegisterConCommandTriggeredCallback(__int64 sqvm);

///////////////////////////////////////////////////////////////////////////////
class VConCommandCallback : public IDetour
{
	virtual void GetAdr(void) const;
	virtual void GetFun(void) const;
	virtual void GetVar(void) const;
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // GAME_CLIENT_CONCOMMANDCALLBACK_H
