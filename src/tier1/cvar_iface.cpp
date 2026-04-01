#include "tier1/cvar.h"

extern void ConVarStubs_Init();

static bool CVar_Connect(CCvar* thisptr, CreateInterfaceFn factory)
{
	CCvar__Connect(thisptr, factory);

	ConVar_InitShipped();
	ConVar_PurgeShipped();
	ConCommand_InitShipped();
	ConCommand_PurgeShipped();

	ConVarStubs_Init();
	ConVar_Register();

	// CCvar::Connect() always returns true in the implementation of the engine
	return true;
}

static void CVar_Disconnect(CCvar* thisptr)
{
	ConVar_Unregister();
	CCvar__Disconnect(thisptr);
}

/*
=====================
CON_Help_f

  Shows the colors and
  description of each
  context.
=====================
*/
static void CON_Help_f()
{
	Msg(eDLL_T::COMMON, "Contexts:\n");

	Msg(eDLL_T::SCRIPT_SERVER, " = Server DLL (Script)\n");
	Msg(eDLL_T::SCRIPT_CLIENT, " = Client DLL (Script)\n");
	Msg(eDLL_T::SCRIPT_UI, " = UI DLL (Script)\n");

	Msg(eDLL_T::SERVER, " = Server DLL (Code)\n");
	Msg(eDLL_T::CLIENT, " = Client DLL (Code)\n");
	Msg(eDLL_T::UI, " = UI DLL (Code)\n");

	Msg(eDLL_T::ENGINE, " = Engine DLL (Code)\n");
	Msg(eDLL_T::FS, " = FileSystem (Code)\n");
	Msg(eDLL_T::RTECH, " = PakLoad API (Code)\n");
	Msg(eDLL_T::MS, " = MaterialSystem (Code)\n");

	Msg(eDLL_T::AUDIO, " = Audio DLL (Code)\n");
	Msg(eDLL_T::VIDEO, " = Video DLL (Code)\n");
	Msg(eDLL_T::NETCON, " = NetConsole (Code)\n");
	Msg(eDLL_T::MODSYSTEM, " = Mod System (Code)\n");
}

static ConCommand con_help("con_help", CON_Help_f, "Shows the colors and description of each context", FCVAR_RELEASE);

extern void ConVar_PrintDescription(const ConCommandBase* const pVar);

///////////////////////////////////////////////////////////////////////////////
void VCVar::Detour(const bool bAttach) const
{
	DetourSetup(&CCvar__Connect, &CVar_Connect, bAttach);
	DetourSetup(&CCvar__Disconnect, &CVar_Disconnect, bAttach);

	DetourSetup(&v_ConVar_PrintDescription, &ConVar_PrintDescription, bAttach);
}
