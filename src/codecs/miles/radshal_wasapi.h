#pragma once

/* ==== WASAPI THREAD SERVICE =========================================================================================================================================== */
inline CMemory p_WASAPI_GetAudioDevice;

///////////////////////////////////////////////////////////////////////////////
class VRadShal : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("WASAPI_GetAudioDevice", (void*)p_WASAPI_GetAudioDevice.GetPtr());
	}
	virtual void GetFun(void) const
	{
		// WASAPI audio device initialization function that contains IsDebuggerPresent check
		// Miles (10.0.48): 0x18005F400 - pattern: 48 8B C4 48 89 58 20 55 56 41 54
		// Miles (10.0.62): 0x1800689C0 - pattern: 48 89 5C 24 08 48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57
		p_WASAPI_GetAudioDevice = Module_FindPattern(g_RadAudioSystemDll, "48 89 5C 24 08 48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 70");
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const { }
};
///////////////////////////////////////////////////////////////////////////////
