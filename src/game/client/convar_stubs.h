#ifndef CONVAR_STUBS_H
#define CONVAR_STUBS_H

// Call after ConVar_Register to log which stubs the engine already had.
// Duplicates can then be removed from convar_stubs.cpp.
void ConVarStubs_LogExisting();

#endif // CONVAR_STUBS_H
