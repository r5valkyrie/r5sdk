#ifndef VSCRIPT_PLAYER_H
#define VSCRIPT_PLAYER_H

struct ScriptClassDescriptor_t;

void Script_RegisterPlayerScriptFunctions(ScriptClassDescriptor_t* playerStruct);
void Script_RegisterPlayerScriptSetters(ScriptClassDescriptor_t* playerStruct);
void VScriptPlayer_LevelShutdown();

int VScriptPlayer_GetExtraShieldHealth(void* pPlayer);
int VScriptPlayer_GetExtraShieldTier(void* pPlayer);

#endif // VSCRIPT_PLAYER_H
