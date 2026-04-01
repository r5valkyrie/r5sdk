#ifndef RTECH_RUI_H
#define RTECH_RUI_H

struct RuiInstance_s;

enum class RuiArgumentType_e : u8
{
	TYPE_STRING = 0x1,
	TYPE_ASSET = 0x2,
	TYPE_BOOL = 0x3,
	TYPE_INT = 0x4,
	TYPE_FLOAT = 0x5,
	TYPE_FLOAT2 = 0x6,
	TYPE_FLOAT3 = 0x7,
	TYPE_COLOR_ALPHA = 0x8,
	TYPE_GAMETIME = 0x9,
	TYPE_WALLTIME = 0xA,
	TYPE_UIHANDLE = 0xB,
	TYPE_IMAGE = 0xC,
	TYPE_FONT_FACE = 0xD,
	TYPE_FONT_HASH = 0xE,
	TYPE_ARRAY = 0xF,
};

struct RuiArg_s
{
	RuiArgumentType_e type;
	char unk1;
	i16 valueOffset;
	i16 nameOffset;
	i16 shortHash;
};

struct RuiArgCluster_s
{
	uint16_t argIndex;
	uint16_t argCount;
	char byte1;
	char byte2;
	i16 unk_6;
	i16 unk_8;
	i16 unk_A;
	char unk_C[4];
	i16 unk_10;
};

struct RuiFuncs_s
{
	void (*SetHidden)(RuiInstance_s* const rui);
	void (*SetNoRender)(RuiInstance_s* const rui);
	void (*SetNoRenderAndAssert)(RuiInstance_s* const rui, const char* const errorMsg);
	float (*GetTransformSize)(RuiInstance_s* const rui);
	void (*GetTextSize)(RuiInstance_s* const rui);
	void* unk1;
	void (*ExecuteTransform)(RuiInstance_s* const rui, const int index);
	void (*ExecuteTransformAndResize)(RuiInstance_s* const rui, const int index);
	const char* (*SNPrintF)(RuiInstance_s* const rui, const char* const fmt, ...);
	const char* (*Localize)(RuiInstance_s* const rui, const char* const key, __int64 a3, __int64 a4, __int64 a5, __int64 a6, __int64 a7);
	const char* (*ToUpper)(RuiInstance_s* const rui, char* const text);
	const char* (*FormatNumber)(RuiInstance_s* const rui, const char* fmt, const float number);
	void* unk2;
	void* unk3;
	void* unk4;
	void* unk5;
	void* unk6;
	void* unk7;
	u32(*FindAsset)(RuiInstance_s* const rui, const char* const assetName);
	void* unk8;
	void* unk9;
	void* unk10;
	void* unk11;
	void* unk12;
	void* unk13;
	u32(*StringToHash)(RuiInstance_s* const rui, const char* const assetName);
	void* (*GetActiveColorBlindPaletteColor)(RuiInstance_s* const rui, const int a2, const int a3);
	void* unk14;
};

struct RuiGlobalState_s
{
};

struct RuiHeader_s
{
	char* name;
	void* defaultValues;
	char* transformData;
	float elementWidth;
	float elementHeight;
	float elementWidthRatio;
	float elementHeightRatio;
	char* argNames;
	RuiArgCluster_s* argClusters;
	RuiArg_s* args;
	u16 argCount;
	u16 short42;
	u16 ruiDataStructSize;
	u16 defaultValuesSize;
	u16 styleDescriptorCount;
	u16 unk_4A;
	u16 renderJobCount;
	u16 argClusterCount;
	void* styleDescriptors;
	void* renderJobs;
	void* mappingData;
	void (*ruiFunc)(RuiFuncs_s* const api, RuiGlobalState_s* const state, RuiInstance_s* const rui, char* const values);
	void (*ruiHiddenFunc)(RuiFuncs_s* const api, RuiGlobalState_s* const state, RuiInstance_s* const rui, char* const values);
};

struct RuiInstance_s
{
	RuiHeader_s* header;
	float elementWidth;
	float elementHeight;
	float elementWidthRatio;
	float elementHeightRatio;
	void* str_v1;
	__m128 m128_20;
	_BYTE gap_30[8];
	i64 createTimeStamp;
	_QWORD qword_40;
	bool isHidden;
	bool hasError;
	_BYTE gap_4A[14];
	void* pointer_58;
	_BYTE data[1];
};

//-----------------------------------------------------------------------------
// Function pointers
//-----------------------------------------------------------------------------
inline bool(*v_Rui_Draw)(__int64* a1, __m128* a2, const __m128i* a3, __int64 a4, __m128* a5);
inline void* (*v_Rui_LoadAsset)(const char* szRuiAssetName);

inline __int64(*v_Rui_LookupAsset)(const char* assetPath);

inline int16_t(*v_Rui_GetFontFace)(void);

// Crashes with -1 index; patched in Detour() when compat is active
inline __int64(*v_Rui_SetArgInt_Direct)(__int64 a1, int a2, int a3);
inline __int64(*v_Rui_SetArgFloat_Direct)(__int64 a1, unsigned int a2, __int64 a3);

// Native UIMG hash table lookup
// On miss this returns the "missing" texture index, not -1.
// Rui_AssetLookupCompat reimplements the lookup with proper miss detection.
inline __int64(*v_Rui_AssetLookup)(__int64 a1, const char* assetName);

inline void(*v_UIMG_BuildHashTable)(__int64 a1, __int64 a2, __int64 a3);

inline unsigned __int64(*v_Rui_EstimateTextWidth)(unsigned __int8* text, unsigned __int16 fontIndex, char uppercase);

// UIMG global hash table (populated at runtime by UIMG_BuildHashTable)
struct UIMGHashEntry
{
	u32 hash;
	u32 unused;
};

inline UIMGHashEntry** g_ppUIMGHashEntries = nullptr;  // hash entry array
inline i32**           g_ppUIMGHashBuckets = nullptr;   // bucket array
inline u32*            g_pnUIMGHashMask = nullptr;      // bucket mask (power_of_2 - 1)

inline u64 (*v_StringToGuid)(const char* str) = nullptr;

inline RuiFuncs_s* s_ruiApi = nullptr;

///////////////////////////////////////////////////////////////////////////////
class V_Rui : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("Rui_Draw", v_Rui_Draw);
		LogFunAdr("Rui_LoadAsset", v_Rui_LoadAsset);
		LogFunAdr("Rui_LookupAsset", v_Rui_LookupAsset);
		LogFunAdr("Rui_GetFontFace", v_Rui_GetFontFace);
		LogFunAdr("Rui_SetArgInt_Direct", v_Rui_SetArgInt_Direct);
		LogFunAdr("Rui_SetArgFloat_Direct", v_Rui_SetArgFloat_Direct);
		LogFunAdr("Rui_AssetLookup", v_Rui_AssetLookup);
		LogFunAdr("Rui_EstimateTextWidth", v_Rui_EstimateTextWidth);
		LogFunAdr("UIMG_BuildHashTable", v_UIMG_BuildHashTable);
		LogVarAdr("s_ruiApi", s_ruiApi);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "40 53 48 83 EC 40 4C 8B 5A 18").GetPtr(v_Rui_Draw);
		Module_FindPattern(g_GameDll, "E8 ?? ?? ?? ?? EB 03 49 8B C6 48 89 86 ?? ?? ?? ?? 8B 86 ?? ?? ?? ??").FollowNearCallSelf().GetPtr(v_Rui_LoadAsset);
		Module_FindPattern(g_GameDll, "4C 8D 05 ?? ?? ?? ?? 4C 8B D9 4D 2B D8 48 8B D9 41 B9 03 00 00 00").Offset(-6).GetPtr(v_Rui_LookupAsset);
		Module_FindPattern(g_GameDll, "F7 05 ?? ?? ?? ?? ?? ?? ?? ?? 4C 8D 0D ?? ?? ?? ?? 74 05 49 8B D1 EB 19 48 8B 05 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 48 8B 48 58 48 85 C9 48 0F 45 D1 F7 05 ?? ?? ?? ?? ?? ?? ?? ?? 75 19 48 8B 05 ?? ?? ?? ?? 4C 8D 0D ?? ?? ?? ?? 4C 8B 40 58 4D 85 C0 4D 0F 45 C8 49 8B C9 48 FF 25 ?? ?? ?? ??").GetPtr(v_Rui_GetFontFace);
		// Skip pattern scan if early patcher already set these (patched bytes break pattern)
		if (!v_Rui_SetArgInt_Direct)
			Module_FindPattern(g_GameDll, "48 8B 41 58 44 0F B6 48 1E 48 8B 81 10 01 00 00").GetPtr(v_Rui_SetArgInt_Direct);
		if (!v_Rui_SetArgFloat_Direct)
			Module_FindPattern(g_GameDll, "48 8B 41 60 8B D2 4C 89 04 D0").GetPtr(v_Rui_SetArgFloat_Direct);
		// RVA-based (pattern scan fails due to missing REX prefix)
		v_Rui_AssetLookup = reinterpret_cast<decltype(v_Rui_AssetLookup)>(
			g_GameDll.GetModuleBase() + 0x2FBF00);
		v_StringToGuid = reinterpret_cast<decltype(v_StringToGuid)>(
			g_GameDll.GetModuleBase() + 0x46CD80);
		v_Rui_EstimateTextWidth = reinterpret_cast<decltype(v_Rui_EstimateTextWidth)>(
			g_GameDll.GetModuleBase() + 0x2F5190);
		v_UIMG_BuildHashTable = reinterpret_cast<decltype(v_UIMG_BuildHashTable)>(
			g_GameDll.GetModuleBase() + 0x2F39C0);
	}
	virtual void GetVar(void) const
	{
		CMemory(v_Rui_Draw).Offset(0x16B).FindPatternSelf("48 8D").ResolveRelativeAddressSelf(3, 7).GetPtr(s_ruiApi);

		// UIMG hash table globals — dereferenced at lookup time (populated when atlas loads)
		g_ppUIMGHashEntries = reinterpret_cast<UIMGHashEntry**>(g_GameDll.GetModuleBase() + 0xC05F2A0);
		g_ppUIMGHashBuckets = reinterpret_cast<i32**>(g_GameDll.GetModuleBase() + 0xC05F2A8);
		g_pnUIMGHashMask = reinterpret_cast<u32*>(g_GameDll.GetModuleBase() + 0xC05F2C0);
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // RTECH_RUI_H
