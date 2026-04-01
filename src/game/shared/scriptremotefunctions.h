#ifndef SCRIPTREMOTEFUNCTIONS_H
#define SCRIPTREMOTEFUNCTIONS_H
#include "thirdparty/detours/include/idetour.h"

constexpr int SCRIPT_REMOTE_ARG_BUFFER_SIZE = 8192;

inline char(*v_ScriptRemote_AddEntry)(__int64 a1, __int64 a2, char a3, char a4, char a5, void* Src);
inline __int64(*v_ScriptRemote_RegisterName)(__int64 a1, unsigned char* a2);

char ScriptRemote_AddEntry(__int64 a1, __int64 a2, char a3, char a4, char a5, void* Src);
__int64 ScriptRemote_RegisterName(__int64 a1, unsigned char* a2);

class VScriptRemoteFunctions : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("ScriptRemote_AddEntry", v_ScriptRemote_AddEntry);
		LogFunAdr("ScriptRemote_RegisterName", v_ScriptRemote_RegisterName);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC 20 44 0F BE 54 24 ?? B8 00 08 00 00").GetPtr(v_ScriptRemote_AddEntry);
		Module_FindPattern(g_GameDll, "48 89 4C 24 ?? 57 48 83 EC 30 8B 05 ?? ?? ?? ?? 48 8B FA 83 F8 01").GetPtr(v_ScriptRemote_RegisterName);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};

#endif // SCRIPTREMOTEFUNCTIONS_H
