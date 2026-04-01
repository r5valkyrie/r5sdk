#ifndef HIGHLIGHT_CONTEXT_H
#define HIGHLIGHT_CONTEXT_H

#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "thirdparty/detours/include/idetour.h"

void HighlightContext_LevelShutdown();
void HighlightContext_RegisterDrawFuncEnum(HSQUIRRELVM v);

// Global function natives
SQRESULT Script_HighlightContext_GetId(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_SetParam(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_GetParam(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_SetDrawFunc(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_GetDrawFunc(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_SetRadius(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_GetOutlineRadius(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_GetInsideFunction(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_GetOutlineFunction(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_SetFlags(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_SetNearFadeDistance(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_SetFarFadeDistance(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_SetFocusedColor(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_IsEntityVisible(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_IsAfterPostProcess(HSQUIRRELVM v);

// Engine: Highlight_SetCurrentContext(entity*, int contextId)
// Engine: Highlight_SetCurrentContext — detoured to remap virtual context IDs
inline void(*v_Highlight_SetCurrentContext)(void* entity, int contextId) = nullptr;

///////////////////////////////////////////////////////////////////////////////
class VHighlightContext : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("Highlight_SetCurrentContext", v_Highlight_SetCurrentContext);
	}
	virtual void GetFun(void) const
	{
		// Highlight_SetCurrentContext (CLIENT)
		// push rdi; sub rsp,20h; lea eax,[rdx+1]; mov rdi,rcx; cmp eax,8
		Module_FindPattern(g_GameDll,
			"40 57 48 83 EC 20 8D 42 01 48 8B F9 83 F8 08")
			.GetPtr(v_Highlight_SetCurrentContext);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // HIGHLIGHT_CONTEXT_H
