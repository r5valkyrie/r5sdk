#ifndef VSCRIPT_REMOTEFUNCTIONS_H
#define VSCRIPT_REMOTEFUNCTIONS_H

class CSquirrelVM;

void Script_RegisterRemoteFunctionNatives(CSquirrelVM* s);
void Script_RegisterRemoteFunctionServerNatives(CSquirrelVM* s);
void Script_RegisterRemoteFunctionUINatives(CSquirrelVM* s);
void Script_ClearRemoteFunctionRegistrations();

#endif // VSCRIPT_REMOTEFUNCTIONS_H
