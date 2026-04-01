//=============================================================================//
//
// Purpose: Activity registration system (ACT_*, ACT_VM_*, etc.)
//
//=============================================================================//
#include "core/stdafx.h"
#include "tier1/convar.h"
#include "filesystem/filesystem.h"
#include "activity.h"
#include <vector>
#include <string>

static std::vector<std::pair<std::string, int>> s_customActivities;
static bool s_initialized = false;
static bool s_serverInitialized = false;
static int s_predefinedMaxId = -1;
static int s_predefinedMaxId_Server = -1;

int RegisterCustomActivity(const char* activityName)
{
	if (!activityName || !*activityName)
	{
		Warning(eDLL_T::ENGINE, "[ACTIVITY] Cannot register empty activity name\n");
		return -1;
	}

	if (!IsActivitySystemInitialized())
	{
		Warning(eDLL_T::ENGINE, "[ACTIVITY] Cannot register activity '%s' - system not initialized\n", activityName);
		return -1;
	}

	if (!v_ActivityList_RegisterActivity || !g_pMaxActivityId)
	{
		Warning(eDLL_T::ENGINE, "[ACTIVITY] ActivityList_RegisterActivity not found\n");
		return -1;
	}

	if (s_predefinedMaxId < 0)
		s_predefinedMaxId = *g_pMaxActivityId;

	int activityId = *g_pMaxActivityId + 1;

	v_ActivityList_RegisterActivity(activityName, activityId, 0);

	if (v_ActivityList_RegisterActivity_Server && g_pMaxActivityId_Server)
	{
		if (s_predefinedMaxId_Server < 0)
			s_predefinedMaxId_Server = *g_pMaxActivityId_Server;

		v_ActivityList_RegisterActivity_Server(activityName, activityId, 0);
	}

	s_customActivities.push_back({ activityName, activityId });
	return activityId;
}

bool IsActivitySystemInitialized()
{
	if (!s_initialized || !g_pActivityList || !g_pMaxActivityId)
		return false;

	if (*g_pMaxActivityId < 100)
		return false;

	return true;
}

int GetActivityCount()
{
	if (!g_pMaxActivityId)
		return 0;
	return *g_pMaxActivityId + 1;
}

void ListAllActivities()
{
	if (!IsActivitySystemInitialized())
	{
		Warning(eDLL_T::ENGINE, "[ACTIVITY] System not initialized\n");
		return;
	}
	
	if (!s_customActivities.empty())
	{
		Msg(eDLL_T::ENGINE, "[ACTIVITY] Custom activities (%zu):\n", s_customActivities.size());
		for (const auto& act : s_customActivities)
			Msg(eDLL_T::ENGINE, "  ID %4d: %s\n", act.second, act.first.c_str());
	}
}

int FindActivityByName(const char* name)
{
	if (!name || !*name)
		return -1;

	for (const auto& act : s_customActivities)
	{
		if (act.first == name)
			return act.second;
	}

	if (v_ActivityList_GetActivityName && g_pMaxActivityId)
	{
		int maxId = *g_pMaxActivityId;
		for (int i = 0; i <= maxId; i++)
		{
			const char* actName = v_ActivityList_GetActivityName(i);
			if (actName && actName[0] != '\0' && strcmp(actName, name) == 0)
				return i;
		}
	}

	return -1;
}

static char* TrimWhitespace(char* str)
{
	if (!str)
		return str;

	while (*str && (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n'))
		str++;

	if (*str == '\0')
		return str;

	char* end = str + strlen(str) - 1;
	while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
		end--;
	*(end + 1) = '\0';

	return str;
}

void ClearCustomActivities()
{
	s_customActivities.clear();
	Msg(eDLL_T::ENGINE, "[ACTIVITY] Cleared %zu custom activities from tracking\n", s_customActivities.size());
}

int LoadCustomActivitiesFromFile()
{
	const char* filePath = "scripts/activity_types.txt";

	if (!FileSystem())
	{
		Warning(eDLL_T::ENGINE, "[ACTIVITY] FileSystem not available\n");
		return 0;
	}

	FileHandle_t pFile = FileSystem()->Open(filePath, "r", "GAME");
	if (!pFile)
	{
		Msg(eDLL_T::ENGINE, "[ACTIVITY] No custom activity file found at %s\n", filePath);
		return 0;
	}

	char line[256];
	int count = 0;
	int lineNum = 0;

	while (FileSystem()->ReadLine(line, sizeof(line), pFile))
	{
		lineNum++;

		char* trimmed = TrimWhitespace(line);

		if (!trimmed[0])
			continue;

		if (trimmed[0] == '/' && trimmed[1] == '/')
			continue;

		char* comment = strstr(trimmed, "//");
		if (comment)
			*comment = '\0';

		trimmed = TrimWhitespace(trimmed);
		if (!trimmed[0])
			continue;

		if (strncmp(trimmed, "ACT_", 4) != 0)
		{
			Warning(eDLL_T::ENGINE, "[ACTIVITY] %s:%d: Invalid name '%s' (must start with ACT_)\n",
				filePath, lineNum, trimmed);
			continue;
		}

		bool duplicate = false;
		for (const auto& act : s_customActivities)
		{
			if (act.first == trimmed)
			{
				Warning(eDLL_T::ENGINE, "[ACTIVITY] %s:%d: Duplicate '%s' - skipping\n",
					filePath, lineNum, trimmed);
				duplicate = true;
				break;
			}
		}

		if (duplicate)
			continue;

		int id = RegisterCustomActivity(trimmed);
		if (id >= 0)
			count++;
	}

	FileSystem()->Close(pFile);
	Msg(eDLL_T::ENGINE, "[ACTIVITY] Loaded %d custom activities from %s\n", count, filePath);

	return count;
}

void VActivityList::GetFun(void) const
{
	Module_FindPattern(g_GameDll, "48 89 5C 24 08 48 89 74 24 10 44 88 44 24 18 57 48 83 EC 20").GetPtr(v_ActivityList_RegisterActivity);
	Module_FindPattern(g_GameDll, "57 48 83 EC 20 44 8B 05 ?? ?? ?? ?? 33 FF 8B C7 45 85 C0").GetPtr(v_ActivityList_GetActivityName);

	if (!v_ActivityList_RegisterActivity)
		Warning(eDLL_T::ENGINE, "[ACTIVITY] ActivityList_RegisterActivity (client) not found\n");

	if (!v_ActivityList_GetActivityName)
		Warning(eDLL_T::ENGINE, "[ACTIVITY] ActivityList_GetActivityName (client) not found\n");

	const uintptr_t base = g_GameDll.GetModuleBase();
	v_ActivityList_RegisterActivity_Server = reinterpret_cast<decltype(v_ActivityList_RegisterActivity_Server)>(base + 0xB38DA0);
	v_ActivityList_GetActivityName_Server  = reinterpret_cast<decltype(v_ActivityList_GetActivityName_Server)> (base + 0xB38F21);
}

void VActivityList::GetVar(void) const
{
	if (v_ActivityList_RegisterActivity)
	{
		CMemory func((uintptr_t)v_ActivityList_RegisterActivity);

		// lea rcx, [activity_list] at +0x19
		CMemory leaActivityList = func.Offset(0x19);
		if (*(uint8_t*)leaActivityList.GetPtr() == 0x48 &&
		    *(uint8_t*)(leaActivityList.GetPtr() + 1) == 0x8D &&
		    *(uint8_t*)(leaActivityList.GetPtr() + 2) == 0x0D)
		{
			g_pActivityList = leaActivityList.ResolveRelativeAddress(0x3, 0x7).GetPtr();
		}

		// mov rcx, [symbol_table] at +0x36
		CMemory movSymbolTable = func.Offset(0x36);
		if (*(uint8_t*)movSymbolTable.GetPtr() == 0x48 &&
		    *(uint8_t*)(movSymbolTable.GetPtr() + 1) == 0x8B &&
		    *(uint8_t*)(movSymbolTable.GetPtr() + 2) == 0x0D)
		{
			g_pActivitySymbolTable = movSymbolTable.ResolveRelativeAddress(0x3, 0x7).GetPtr();
		}

		// mov ecx, [max_id] at +0x4F
		CMemory movMaxId = func.Offset(0x4F);
		if (*(uint8_t*)movMaxId.GetPtr() == 0x8B &&
		    *(uint8_t*)(movMaxId.GetPtr() + 1) == 0x0D)
		{
			g_pMaxActivityId = movMaxId.ResolveRelativeAddress(0x2, 0x6).RCast<int*>();
		}
	}

	if (v_ActivityList_RegisterActivity && g_pActivityList && g_pMaxActivityId)
	{
		s_initialized = true;
	}
	else
	{
		Warning(eDLL_T::ENGINE, "[ACTIVITY] Client system init failed: RegisterActivity=%s List=%s MaxId=%s\n",
			v_ActivityList_RegisterActivity ? "OK" : "MISSING",
			g_pActivityList ? "OK" : "MISSING",
			g_pMaxActivityId ? "OK" : "MISSING");
	}

	if (v_ActivityList_RegisterActivity_Server)
	{
		CMemory func((uintptr_t)v_ActivityList_RegisterActivity_Server);

		CMemory leaActivityList = func.Offset(0x19);
		if (*(uint8_t*)leaActivityList.GetPtr() == 0x48 &&
		    *(uint8_t*)(leaActivityList.GetPtr() + 1) == 0x8D &&
		    *(uint8_t*)(leaActivityList.GetPtr() + 2) == 0x0D)
		{
			g_pActivityList_Server = leaActivityList.ResolveRelativeAddress(0x3, 0x7).GetPtr();
		}

		CMemory movSymbolTable = func.Offset(0x36);
		if (*(uint8_t*)movSymbolTable.GetPtr() == 0x48 &&
		    *(uint8_t*)(movSymbolTable.GetPtr() + 1) == 0x8B &&
		    *(uint8_t*)(movSymbolTable.GetPtr() + 2) == 0x0D)
		{
			g_pActivitySymbolTable_Server = movSymbolTable.ResolveRelativeAddress(0x3, 0x7).GetPtr();
		}

		CMemory movMaxId = func.Offset(0x4F);
		if (*(uint8_t*)movMaxId.GetPtr() == 0x8B &&
		    *(uint8_t*)(movMaxId.GetPtr() + 1) == 0x0D)
		{
			g_pMaxActivityId_Server = movMaxId.ResolveRelativeAddress(0x2, 0x6).RCast<int*>();
		}
	}

	if (v_ActivityList_RegisterActivity_Server && g_pActivityList_Server && g_pMaxActivityId_Server)
	{
		s_serverInitialized = true;
	}
	else
	{
		g_pActivityList_Server = 0;
		g_pActivitySymbolTable_Server = 0;
		g_pMaxActivityId_Server = nullptr;
		s_serverInitialized = false;
	}
}

void VActivityList::Detour(const bool bAttach) const
{
}

static void CC_ActivityList_Dump(const CCommand& args)
{
	if (!IsActivitySystemInitialized())
	{
		Warning(eDLL_T::ENGINE, "[ACTIVITY] System not initialized\n");
		return;
	}

	const char* filter = (args.ArgC() > 1) ? args.Arg(1) : nullptr;

	if (!v_ActivityList_GetActivityName || !g_pMaxActivityId)
	{
		Warning(eDLL_T::ENGINE, "[ACTIVITY] GetActivityName not found\n");
		return;
	}

	int maxId = *g_pMaxActivityId;
	int totalCount = 0;
	int shownCount = 0;
	int customCount = 0;

	Msg(eDLL_T::ENGINE, "[ACTIVITY] Listing activities%s%s%s:\n",
		filter ? " matching '" : "", filter ? filter : "", filter ? "'" : "");
	Msg(eDLL_T::ENGINE, "--------------------------------------------\n");

	for (int i = 0; i <= maxId; i++)
	{
		const char* name = v_ActivityList_GetActivityName(i);
		if (name && name[0] != '\0' && strcmp(name, "(invalid activity index)") != 0)
		{
			totalCount++;

			bool isCustom = (s_predefinedMaxId >= 0 && i > s_predefinedMaxId);
			if (isCustom) customCount++;

			if (!filter || strstr(name, filter) != nullptr)
			{
				Msg(eDLL_T::ENGINE, "  [%3d] %s%s\n", i, name, isCustom ? " (custom)" : "");
				shownCount++;
			}
		}
	}

	Msg(eDLL_T::ENGINE, "--------------------------------------------\n");
	Msg(eDLL_T::ENGINE, "[ACTIVITY] Shown %d / %d (predefined: %d, custom: %d)\n",
		shownCount, totalCount, totalCount - customCount, customCount);
}

static ConCommand activity_dump("activity_dump", CC_ActivityList_Dump, "List registered activities. Usage: activity_dump [filter]", FCVAR_RELEASE);

static void CC_ActivityList_Reload(const CCommand& args)
{
	if (!IsActivitySystemInitialized())
	{
		Warning(eDLL_T::ENGINE, "[ACTIVITY] System not initialized\n");
		return;
	}

	ClearCustomActivities();
	LoadCustomActivitiesFromFile();
}

static ConCommand activity_reload("activity_reload", CC_ActivityList_Reload, "Reload custom activities from scripts/activity_types.txt", FCVAR_RELEASE);
