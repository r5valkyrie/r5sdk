//=============================================================================//
//
// Purpose: StatusEffect_GetTotalSeverity - SDK native implementation
//          Provides SUM semantics for status effect severity accumulation.
//
//=============================================================================//
#ifndef STATUS_EFFECTS_SDK_H
#define STATUS_EFFECTS_SDK_H

class CSquirrelVM;

void StatusEffects_SDK_RegisterServerFunctions(CSquirrelVM* vm);
void StatusEffects_SDK_RegisterClientFunctions(CSquirrelVM* vm);
void StatusEffects_SDK_RegisterUIFunctions(CSquirrelVM* vm);

#endif // STATUS_EFFECTS_SDK_H
