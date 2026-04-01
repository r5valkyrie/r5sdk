//=============================================================================//
//
// Purpose: Activity modifier registration system
//
//=============================================================================//
#ifndef ACTIVITYMODIFIER_H
#define ACTIVITYMODIFIER_H

#include "tier1/utlsymbol.h"

// Wraps CUtlSymbolTableMT::AddString; used to register modifier strings
inline CUtlSymbol* (*v_AddActivityModifierString)(void* table, CUtlSymbol* result, const char* modifier);

inline uintptr_t   g_pActivityModifiersTable;
inline const char** g_ActivityModifierNames;
inline CUtlSymbol* g_ActivityModifierSymbols;
inline int         g_nActivityModifierCount;

CUtlSymbol  RegisterCustomActivityModifier(const char* modifierName);
CUtlSymbol  FindActivityModifier(const char* modifierName);
bool        IsActivityModifierSystemInitialized();
const char* GetActivityModifierName(int index);
CUtlSymbol  GetActivityModifierSymbol(int index);
void        DebugActivityModifierState();
void        ListAllActivityModifiers();
void        ClearCustomActivityModifiers();
int         LoadCustomActivityModifiersFromFile();

///////////////////////////////////////////////////////////////////////////////
class VActivityModifiers : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("AddActivityModifierString", v_AddActivityModifierString);
		LogVarAdr("g_pActivityModifiersTable", reinterpret_cast<void*>(g_pActivityModifiersTable));
		LogVarAdr("g_ActivityModifierNames", reinterpret_cast<void*>(g_ActivityModifierNames));
		LogVarAdr("g_ActivityModifierSymbols", reinterpret_cast<void*>(g_ActivityModifierSymbols));
	}
	virtual void GetFun(void) const;
	virtual void GetVar(void) const;
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // ACTIVITYMODIFIER_H
