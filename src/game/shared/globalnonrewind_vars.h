#ifndef GLOBALNONREWIND_VARS_H
#define GLOBALNONREWIND_VARS_H

#include "vscript/languages/squirrel_re/include/sqvm.h"

// Global NonRewind net vars (Get/Set by name)
SQRESULT Script_SetGlobalNonRewindNetBool(HSQUIRRELVM v);
SQRESULT Script_SetGlobalNonRewindNetInt(HSQUIRRELVM v);
SQRESULT Script_SetGlobalNonRewindNetFloat(HSQUIRRELVM v);
SQRESULT Script_SetGlobalNonRewindNetTime(HSQUIRRELVM v);
SQRESULT Script_SetGlobalNonRewindNetEnt(HSQUIRRELVM v);
SQRESULT Script_GetGlobalNonRewindNetBool(HSQUIRRELVM v);
SQRESULT Script_GetGlobalNonRewindNetInt(HSQUIRRELVM v);
SQRESULT Script_GetGlobalNonRewindNetFloat(HSQUIRRELVM v);
SQRESULT Script_GetGlobalNonRewindNetTime(HSQUIRRELVM v);
SQRESULT Script_GetGlobalNonRewindNetEnt(HSQUIRRELVM v);

// Per-player NonRewind (player methods)
SQRESULT Script_GetNonRewindRespawnTime(HSQUIRRELVM v);
SQRESULT Script_SetNonRewindRespawnTime(HSQUIRRELVM v);
SQRESULT Script_GetNonRewindMusicPack(HSQUIRRELVM v);
SQRESULT Script_SetNonRewindMusicPack(HSQUIRRELVM v);

void GlobalNonRewind_LevelShutdown();

#endif // GLOBALNONREWIND_VARS_H
