//=============================================================================//
//
// Purpose: Activity modifier registration system
//
//=============================================================================//
#include "core/stdafx.h"
#include "tier1/convar.h"
#include "filesystem/filesystem.h"
#include "activitymodifier.h"
#include <vector>
#include <string>

static std::vector<std::pair<std::string, CUtlSymbol>> s_customModifiers;
static bool s_initialized = false;

CUtlSymbol RegisterCustomActivityModifier(const char* modifierName)
{
	if (!modifierName || !*modifierName)
	{
		Warning(eDLL_T::ENGINE, "[ACTMOD] Cannot register empty modifier name\n");
		return CUtlSymbol();
	}

	if (!IsActivityModifierSystemInitialized())
	{
		Warning(eDLL_T::ENGINE, "[ACTMOD] Cannot register modifier '%s' - system not initialized\n", modifierName);
		return CUtlSymbol();
	}

	if (!v_AddActivityModifierString || !g_pActivityModifiersTable)
	{
		Warning(eDLL_T::ENGINE, "[ACTMOD] AddActivityModifierString or table not found\n");
		return CUtlSymbol();
	}

	CUtlSymbol result;
	v_AddActivityModifierString(reinterpret_cast<void*>(g_pActivityModifiersTable), &result, modifierName);

	if (!result.IsValid())
		Warning(eDLL_T::ENGINE, "[ACTMOD] Failed to register modifier '%s'\n", modifierName);
	else
		s_customModifiers.push_back({ modifierName, result });

	return result;
}

CUtlSymbol FindActivityModifier(const char* modifierName)
{
	if (!modifierName || !*modifierName)
		return CUtlSymbol();

	if (!IsActivityModifierSystemInitialized())
		return CUtlSymbol();

	for (const auto& mod : s_customModifiers)
	{
		if (mod.first == modifierName)
			return mod.second;
	}

	if (g_ActivityModifierNames && g_ActivityModifierSymbols)
	{
		for (int i = 0; i < g_nActivityModifierCount; i++)
		{
			if (g_ActivityModifierNames[i] && strcmp(g_ActivityModifierNames[i], modifierName) == 0)
				return g_ActivityModifierSymbols[i];
		}
	}

	return CUtlSymbol();
}

bool IsActivityModifierSystemInitialized()
{
	if (!s_initialized || !g_pActivityModifiersTable || !g_ActivityModifierSymbols)
		return false;

	// The CUtlSymbolTableMT internal tree pointer at +8 is NULL until the table is ready
	uintptr_t tableTreePtr = *reinterpret_cast<uintptr_t*>(g_pActivityModifiersTable + 8);
	if (tableTreePtr == 0)
		return false;

	if (g_ActivityModifierSymbols[0].IsValid() == false)
		return false;

	return true;
}

void DebugActivityModifierState()
{
	Msg(eDLL_T::ENGINE, "[ACTMOD DEBUG] s_initialized=%d, table=0x%llX, symbols=0x%llX\n",
		s_initialized, g_pActivityModifiersTable, (uintptr_t)g_ActivityModifierSymbols);

	if (g_pActivityModifiersTable)
	{
		uintptr_t treePtr = *reinterpret_cast<uintptr_t*>(g_pActivityModifiersTable + 8);
		Msg(eDLL_T::ENGINE, "[ACTMOD DEBUG] table+8 (tree)=0x%llX\n", treePtr);
	}

	if (g_ActivityModifierSymbols)
	{
		Msg(eDLL_T::ENGINE, "[ACTMOD DEBUG] symbols[0]=%u (valid=%d)\n",
			(unsigned)g_ActivityModifierSymbols[0], g_ActivityModifierSymbols[0].IsValid());
	}
}

void ListAllActivityModifiers()
{
	if (!g_ActivityModifierNames || !g_ActivityModifierSymbols)
	{
		Warning(eDLL_T::ENGINE, "[ACTMOD] System not initialized\n");
		return;
	}

	Msg(eDLL_T::ENGINE, "[ACTMOD] Listing %d predefined activity modifiers:\n", g_nActivityModifierCount);
	Msg(eDLL_T::ENGINE, "--------------------------------------------\n");

	for (int i = 0; i < g_nActivityModifierCount; i++)
	{
		const char* name = g_ActivityModifierNames[i];
		CUtlSymbol symbol = g_ActivityModifierSymbols[i];

		if (name)
			Msg(eDLL_T::ENGINE, "  [%2d] Symbol %3u: %s\n", i, (unsigned)symbol, name);
		else
			Msg(eDLL_T::ENGINE, "  [%2d] Symbol %3u: <null>\n", i, (unsigned)symbol);
	}

	Msg(eDLL_T::ENGINE, "--------------------------------------------\n");

	if (!s_customModifiers.empty())
	{
		Msg(eDLL_T::ENGINE, "[ACTMOD] Custom modifiers (%zu):\n", s_customModifiers.size());
		for (const auto& mod : s_customModifiers)
			Msg(eDLL_T::ENGINE, "  Symbol %3u: %s\n", (unsigned)mod.second, mod.first.c_str());
	}
}

const char* GetActivityModifierName(int index)
{
	if (!g_ActivityModifierNames || index < 0 || index >= g_nActivityModifierCount)
		return nullptr;
	return g_ActivityModifierNames[index];
}

CUtlSymbol GetActivityModifierSymbol(int index)
{
	if (!g_ActivityModifierSymbols || index < 0 || index >= g_nActivityModifierCount)
		return CUtlSymbol();
	return g_ActivityModifierSymbols[index];
}

static char* TrimWhitespaceModifier(char* str)
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

void ClearCustomActivityModifiers()
{
	size_t count = s_customModifiers.size();
	s_customModifiers.clear();
	Msg(eDLL_T::ENGINE, "[ACTMOD] Cleared %zu custom modifiers from tracking\n", count);
}

int LoadCustomActivityModifiersFromFile()
{
	const char* filePath = "scripts/activity_modifier_types.txt";

	if (!FileSystem())
	{
		Warning(eDLL_T::ENGINE, "[ACTMOD] FileSystem not available\n");
		return 0;
	}

	FileHandle_t pFile = FileSystem()->Open(filePath, "r", "GAME");
	if (!pFile)
	{
		Msg(eDLL_T::ENGINE, "[ACTMOD] No custom modifier file found at %s\n", filePath);
		return 0;
	}

	char line[256];
	int count = 0;
	int lineNum = 0;

	while (FileSystem()->ReadLine(line, sizeof(line), pFile))
	{
		lineNum++;

		char* trimmed = TrimWhitespaceModifier(line);

		if (!trimmed[0])
			continue;

		if (trimmed[0] == '/' && trimmed[1] == '/')
			continue;

		char* comment = strstr(trimmed, "//");
		if (comment)
			*comment = '\0';

		trimmed = TrimWhitespaceModifier(trimmed);
		if (!trimmed[0])
			continue;

		bool duplicate = false;
		for (const auto& mod : s_customModifiers)
		{
			if (mod.first == trimmed)
			{
				Warning(eDLL_T::ENGINE, "[ACTMOD] %s:%d: Duplicate '%s' - skipping\n",
					filePath, lineNum, trimmed);
				duplicate = true;
				break;
			}
		}

		if (!duplicate && g_ActivityModifierNames)
		{
			for (int i = 0; i < g_nActivityModifierCount; i++)
			{
				if (g_ActivityModifierNames[i] && strcmp(g_ActivityModifierNames[i], trimmed) == 0)
				{
					Warning(eDLL_T::ENGINE, "[ACTMOD] %s:%d: Modifier '%s' already predefined - skipping\n",
						filePath, lineNum, trimmed);
					duplicate = true;
					break;
				}
			}
		}

		if (duplicate)
			continue;

		CUtlSymbol symbol = RegisterCustomActivityModifier(trimmed);
		if (symbol.IsValid())
			count++;
	}

	FileSystem()->Close(pFile);
	Msg(eDLL_T::ENGINE, "[ACTMOD] Loaded %d custom modifiers from %s\n", count, filePath);

	return count;
}

void VActivityModifiers::GetFun(void) const
{
	Module_FindPattern(g_GameDll, "48 89 54 24 ?? 53 56 41 54 41 57 48 83 EC 38 4D 8B F8 48 8B DA 48 8B F1 4D 85 C0").GetPtr(v_AddActivityModifierString);

	if (!v_AddActivityModifierString)
		Warning(eDLL_T::ENGINE, "[ACTMOD] AddActivityModifierString not found\n");
}

void VActivityModifiers::GetVar(void) const
{
	// Locate the modifier initialization loop by its distinctive preamble:
	//   lea rsi, [ActivityModifierSymbols]   (at pattern - 7)
	//   mov r15d, 10000h                     (pattern anchor)
	//   lea rbx, [ActivityModifierNames]     (at pattern + 6)
	//   lea r12, [ActivityModifierNames_end] (at pattern + 13)
	//   lea rcx, [g_ActivityModifiersTable]  (at pattern + 0x5C)
	//   NOTE: +0x52 points to the table lock, not the table itself
	CMemory initPattern = Module_FindPattern(g_GameDll, "41 BF 00 00 01 00 48 8D 1D");
	if (initPattern)
	{
		CMemory leaNames = initPattern.Offset(6);
		g_ActivityModifierNames = leaNames.ResolveRelativeAddress(0x3, 0x7).RCast<const char**>();

		CMemory leaNamesEnd = initPattern.Offset(13);
		const char** namesEnd = leaNamesEnd.ResolveRelativeAddress(0x3, 0x7).RCast<const char**>();

		if (g_ActivityModifierNames && namesEnd)
			g_nActivityModifierCount = static_cast<int>(namesEnd - g_ActivityModifierNames);

		CMemory leaSymbols = initPattern.Offset(-7);
		if (*(uint8_t*)leaSymbols.GetPtr() == 0x48 &&
		    *(uint8_t*)(leaSymbols.GetPtr() + 1) == 0x8D &&
		    *(uint8_t*)(leaSymbols.GetPtr() + 2) == 0x35)
		{
			g_ActivityModifierSymbols = leaSymbols.ResolveRelativeAddress(0x3, 0x7).RCast<CUtlSymbol*>();
		}

		CMemory tableRef = initPattern.Offset(0x5C);
		if (*(uint8_t*)tableRef.GetPtr() == 0x48 &&
		    *(uint8_t*)(tableRef.GetPtr() + 1) == 0x8D &&
		    *(uint8_t*)(tableRef.GetPtr() + 2) == 0x0D)
		{
			g_pActivityModifiersTable = tableRef.ResolveRelativeAddress(0x3, 0x7).GetPtr();
		}
	}

	if (g_pActivityModifiersTable && g_ActivityModifierNames && g_ActivityModifierSymbols)
	{
		s_initialized = true;
	}
	else
	{
		Warning(eDLL_T::ENGINE, "[ACTMOD] System init failed: Table=%s Names=%s Symbols=%s\n",
			g_pActivityModifiersTable ? "OK" : "MISSING",
			g_ActivityModifierNames ? "OK" : "MISSING",
			g_ActivityModifierSymbols ? "OK" : "MISSING");
	}
}

void VActivityModifiers::Detour(const bool bAttach) const
{
}

static void CC_ActivityModifier_Dump(const CCommand& args)
{
	ListAllActivityModifiers();
}

static ConCommand activitymodifier_dump("activitymodifier_dump", CC_ActivityModifier_Dump, "List registered activity modifiers", FCVAR_RELEASE);

static void CC_ActivityModifier_Reload(const CCommand& args)
{
	if (!IsActivityModifierSystemInitialized())
	{
		Warning(eDLL_T::ENGINE, "[ACTMOD] System not initialized\n");
		return;
	}

	ClearCustomActivityModifiers();
	LoadCustomActivityModifiersFromFile();
}

static ConCommand activitymodifier_reload("activitymodifier_reload", CC_ActivityModifier_Reload, "Reload custom activity modifiers from scripts/activity_modifier_types.txt", FCVAR_RELEASE);
