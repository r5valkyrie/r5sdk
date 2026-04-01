//=============================================================================//
//
// Purpose: Activity registration system (ACT_*, ACT_VM_*, etc.)
//
//=============================================================================//
#ifndef ACTIVITY_SYSTEM_H
#define ACTIVITY_SYSTEM_H

#include "tier1/utlsymbol.h"

inline __int64 (*v_ActivityList_RegisterActivity)(const char* name, int activityId, char flag);
inline const char* (*v_ActivityList_GetActivityName)(int activityId);

inline __int64 (*v_ActivityList_RegisterActivity_Server)(const char* name, int activityId, char flag);
inline const char* (*v_ActivityList_GetActivityName_Server)(int activityId);

inline uintptr_t g_pActivityList;
inline uintptr_t g_pActivitySymbolTable;
inline int* g_pMaxActivityId;

inline uintptr_t g_pActivityList_Server;
inline uintptr_t g_pActivitySymbolTable_Server;
inline int* g_pMaxActivityId_Server;

int  RegisterCustomActivity(const char* activityName);
bool IsActivitySystemInitialized();
int  GetActivityCount();
void ListAllActivities();
int  FindActivityByName(const char* name);
void ClearCustomActivities();
int  LoadCustomActivitiesFromFile();

///////////////////////////////////////////////////////////////////////////////
class VActivityList : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("ActivityList_RegisterActivity", v_ActivityList_RegisterActivity);
		LogFunAdr("ActivityList_GetActivityName", v_ActivityList_GetActivityName);
		LogVarAdr("g_pActivityList", reinterpret_cast<void*>(g_pActivityList));
		LogVarAdr("g_pActivitySymbolTable", reinterpret_cast<void*>(g_pActivitySymbolTable));
		LogVarAdr("g_pMaxActivityId", reinterpret_cast<void*>(g_pMaxActivityId));
		LogFunAdr("ActivityList_RegisterActivity_Server", v_ActivityList_RegisterActivity_Server);
		LogFunAdr("ActivityList_GetActivityName_Server", v_ActivityList_GetActivityName_Server);
		LogVarAdr("g_pActivityList_Server", reinterpret_cast<void*>(g_pActivityList_Server));
		LogVarAdr("g_pActivitySymbolTable_Server", reinterpret_cast<void*>(g_pActivitySymbolTable_Server));
		LogVarAdr("g_pMaxActivityId_Server", reinterpret_cast<void*>(g_pMaxActivityId_Server));
	}
	virtual void GetFun(void) const;
	virtual void GetVar(void) const;
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // ACTIVITY_SYSTEM_H
