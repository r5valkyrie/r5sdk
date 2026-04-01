//=============================================================================//
//
// Purpose: RUI v42.1_custom compatibility layer
//
//=============================================================================//

#include "core/stdafx.h"
#include "rui.h"
#include "tier1/cvar.h"
#include <cmath>

//-----------------------------------------------------------------------------
// ConVars
//-----------------------------------------------------------------------------
static ConVar rui_drawEnable("rui_drawEnable", "1", FCVAR_RELEASE,
	"Enables RUI rendering", false, 0.f, false, 0.f, "1=draw, 0=skip");

//=============================================================================
// GLOBALS
//=============================================================================


#define RUI_MAX_VTABLE_ENTRIES 52

static const char* s_ruiEmptyString = "";
static alignas(16) uint8_t s_index3FallbackBuffer[256] = { 0 };

static void* s_ruiApiExtended[RUI_MAX_VTABLE_ENTRIES] = { nullptr };
static bool s_extendedVtableInitialized = false;
static void* s_originalVtableFuncs[RUI_MAX_VTABLE_ENTRIES] = { nullptr };
static void* s_originalDrawFuncs[6] = { nullptr };

typedef __m128 (*GetTextSize_t)(__int64*, unsigned int);
static GetTextSize_t s_originalGetTextSize = nullptr;

//=============================================================================
// VTABLE UTILITY FUNCTIONS
//=============================================================================

static void Rui_CodeAssert(RuiInstance_s* const ruiInstance, const char* const errorMsg)
{
	if (ruiInstance)
		ruiInstance->hasError = true;
}

static bool Rui_Draw_Wrapper(__int64* a1, __m128* a2, const __m128i* a3, __int64 a4, __m128* a5)
{
	if (!rui_drawEnable.GetBool())
		return false;

	return v_Rui_Draw(a1, a2, a3, a4, a5);
}

//-----------------------------------------------------------------------------
// Index 3: GetWidgetSizeArray
//-----------------------------------------------------------------------------
static void* Rui_GetWidgetSizeArray(__int64 context)
{
	if (!context || static_cast<uintptr_t>(context) < 0x10000)
		return s_index3FallbackBuffer;

	__int64 updateCache = *reinterpret_cast<__int64*>(context + 0x18);
	if (!updateCache)
		return s_index3FallbackBuffer;

	return reinterpret_cast<void*>(updateCache + 0x3E90);
}

//-----------------------------------------------------------------------------
// Index 4: GetTextSize
//-----------------------------------------------------------------------------
static __m128 Rui_GetTextSizeCompat(__int64* context, int param2, int param3)
{
	if (!s_originalGetTextSize)
		return _mm_setzero_ps();

	return s_originalGetTextSize(context, (unsigned int)param2);
}

//-----------------------------------------------------------------------------
// Index 18/22: Asset lookup with UIMG hash table probe
//-----------------------------------------------------------------------------
static i32 UIMG_HashLookup(u32 targetHash)
{
	if (!g_ppUIMGHashEntries || !g_ppUIMGHashBuckets || !g_pnUIMGHashMask)
		return -1;

	UIMGHashEntry* entries = *g_ppUIMGHashEntries;
	i32* buckets = *g_ppUIMGHashBuckets;
	u32 mask = *g_pnUIMGHashMask;

	if (!entries || !buckets || mask == 0)
		return -1;

	if (mask & (mask + 1))
		return -1;

	const u32 tableSize = mask + 1;
	u32 bucket = targetHash & mask;

	for (u32 probes = 0; probes < tableSize; probes++)
	{
		i32 idx = buckets[bucket];
		if (idx < 0)
			return -1;

		if (entries[idx].hash == targetHash)
			return idx;

		bucket = (bucket + 1) & mask;
	}

	return -1;
}

static u32 ComputeUIMGHash(const char* name)
{
	if (!v_StringToGuid)
		return 0;

	u64 guid = v_StringToGuid(name);
	return static_cast<u32>(guid & 0xFFFFFFFF) ^ static_cast<u32>(guid >> 32);
}

static i32 Rui_AssetLookupCompat(__int64 context, const char* assetName)
{
	if (!assetName || *assetName == '\0')
		return -1;

	i32 result = UIMG_HashLookup(ComputeUIMGHash(assetName));
	if (result != -1)
		return result;

	char fullPath[256];
	snprintf(fullPath, sizeof(fullPath), "ui_image/%s.rpak", assetName);
	result = UIMG_HashLookup(ComputeUIMGHash(fullPath));
	if (result != -1)
		return result;

	if (v_Rui_AssetLookup)
		return static_cast<i32>(v_Rui_AssetLookup(context, assetName));

	return -1;
}

//-----------------------------------------------------------------------------
// Index 28/32: NestedUiAutoSize
//-----------------------------------------------------------------------------
static __m128i Rui_NestedUiAutoSize(__int64* context, int nestedIndex)
{
	return _mm_set1_epi32(0x3F800000);
}

//-----------------------------------------------------------------------------
// Index 29/33: FlexSpaceCalc
//-----------------------------------------------------------------------------
#define RUI_WIDGET_SIZE_OFFSET 19264

static float Rui_FlexSpaceCalc(__int64 context, int dimension, int baseIdx, int startIdx, int endIdx)
{
	if (!context || static_cast<uintptr_t>(context) < 0x10000)
		return 1.0f;

	if (static_cast<unsigned int>(dimension) > 3)
		return 1.0f;

	const int widgetCount = *reinterpret_cast<unsigned __int16*>(context + 76);
	__int64 updateCache = *reinterpret_cast<__int64*>(context + 24);
	if (!updateCache)
		return 1.0f;

	if (widgetCount + baseIdx < 0 || widgetCount + baseIdx > 2048)
		return 1.0f;
	if (endIdx < startIdx)
		return 1.0f;
	if (widgetCount + startIdx < 0 || widgetCount + endIdx > 2048)
		return 1.0f;

	const __int64 basePtr = updateCache + RUI_WIDGET_SIZE_OFFSET;

	__m128i baseVec = _mm_shuffle_epi32(*reinterpret_cast<__m128i*>(basePtr + 16LL * (widgetCount + baseIdx)), 216);
	float baseSize = *reinterpret_cast<float*>(&baseVec.m128i_i32[dimension]);
	float remaining = baseSize;

	if (startIdx != endIdx)
	{
		__m128i* rangePtr = reinterpret_cast<__m128i*>(basePtr + 16LL * (widgetCount + startIdx));
		int count = endIdx - startIdx;
		do
		{
			__m128i v = _mm_shuffle_epi32(*rangePtr++, 216);
			remaining = remaining - *reinterpret_cast<float*>(&v.m128i_i32[dimension]);
			--count;
		} while (count > 0);
	}

	if (baseSize == 0.0f)
		return 1.0f;

	return remaining / baseSize;
}

//-----------------------------------------------------------------------------
// Index 30/34: WidgetSizeRatio
//-----------------------------------------------------------------------------
static float Rui_WidgetSizeRatio(__int64 context, int dimension, int denomIdx, int numerBase, int numerIdx)
{
	if (!context || static_cast<uintptr_t>(context) < 0x10000)
		return 1.0f;

	if (static_cast<unsigned int>(dimension) > 3)
		return 1.0f;

	const int widgetCount = *reinterpret_cast<unsigned __int16*>(context + 76);
	__int64 updateCache = *reinterpret_cast<__int64*>(context + 24);
	if (!updateCache)
		return 1.0f;

	if (widgetCount + denomIdx < 0 || widgetCount + denomIdx > 2048)
		return 1.0f;
	int numerTotal = numerIdx + widgetCount + numerBase;
	if (numerTotal < 0 || numerTotal > 2048)
		return 1.0f;

	__m128i denomVec = _mm_shuffle_epi32(
		*reinterpret_cast<__m128i*>(updateCache + 16LL * (widgetCount + denomIdx) + RUI_WIDGET_SIZE_OFFSET), 216);
	float denomSize = *reinterpret_cast<float*>(&denomVec.m128i_i32[dimension]);

	__m128i numerVec = _mm_shuffle_epi32(
		*reinterpret_cast<__m128i*>(updateCache + 16LL * numerTotal + RUI_WIDGET_SIZE_OFFSET), 216);
	float numerSize = *reinterpret_cast<float*>(&numerVec.m128i_i32[dimension]);

	if (denomSize == 0.0f)
		return 1.0f;

	return numerSize / denomSize;
}

//-----------------------------------------------------------------------------
// Index 31/35: MeasureTextByFontHash
//-----------------------------------------------------------------------------
static float Rui_MeasureTextByFontHash(__int64 textPtr, __int64 fontIndex, float scale)
{
	if (!v_Rui_EstimateTextWidth || !textPtr || static_cast<uintptr_t>(textPtr) < 0x10000)
		return 0.0f;

	unsigned __int16 fi = static_cast<unsigned __int16>(fontIndex & 0xFFFF);
	unsigned __int64 result = v_Rui_EstimateTextWidth(reinterpret_cast<unsigned __int8*>(textPtr), fi, 0);
	float maxWidth = *reinterpret_cast<float*>(&result);

	return maxWidth * scale;
}

//-----------------------------------------------------------------------------
// GetLocString stubs (indices 39-45)
//-----------------------------------------------------------------------------
#define RUI_GETLOCSTRING_STUB(idx) \
static const char* Rui_GetLocString_##idx(__int64 context) \
{ \
	return s_ruiEmptyString; \
}

RUI_GETLOCSTRING_STUB(36)
RUI_GETLOCSTRING_STUB(37)
RUI_GETLOCSTRING_STUB(38)
RUI_GETLOCSTRING_STUB(39)
RUI_GETLOCSTRING_STUB(40)
RUI_GETLOCSTRING_STUB(41)
RUI_GETLOCSTRING_STUB(42)

//-----------------------------------------------------------------------------
// Index 38: GuardCheckIcallNop
//-----------------------------------------------------------------------------
static __int64 Rui_GuardCheckIcallNop(__int64 /*context*/)
{
	return 0;
}

//-----------------------------------------------------------------------------
// Index 14: ReturnFalse
//-----------------------------------------------------------------------------
static bool __fastcall Rui_ReturnFalse(__int64 /*context*/)
{
	return false;
}

//-----------------------------------------------------------------------------
// Index 36: ProjectionHelper
//-----------------------------------------------------------------------------
static __m128 __fastcall Rui_ProjectionHelper(__int64 a1, __int64 a2,
	const __m128i* a3, __m128* a4, __m128* a5, __m128* a6)
{
	const __m128 pos = _mm_castsi128_ps(_mm_loadu_si128(a3));
	const __m128 ref = _mm_castsi128_ps(
		_mm_loadl_epi64((const __m128i*)((const char*)a2 + 60)));
	const __m128 posLo = _mm_castsi128_ps(
		_mm_move_epi64(_mm_castps_si128(pos)));
	const __m128 diff = _mm_sub_ps(ref, posLo);
	const __m128 scaled = _mm_mul_ps(diff, *a4);

	const __m128 s0 = _mm_castsi128_ps(
		_mm_shuffle_epi32(_mm_castps_si128(scaled), 0x00));
	const __m128 s1 = _mm_castsi128_ps(
		_mm_shuffle_epi32(_mm_castps_si128(scaled), 0x55));
	const __m128 s2 = _mm_castsi128_ps(
		_mm_shuffle_epi32(_mm_castps_si128(scaled), 0xAA));
	const float distance = _mm_cvtss_f32(
		_mm_add_ps(_mm_add_ps(s1, s0), s2));

	typedef __m128(__fastcall* ProjectWorldPointFunc_t)(
		__int64, __int64, __m128*);
	const auto projFunc = reinterpret_cast<ProjectWorldPointFunc_t>(
		s_originalVtableFuncs[15]);
	__m128 dummyPoint = _mm_setzero_ps();
	const __m128 projRaw = projFunc(a1, a2, &dummyPoint);

	ALIGN16 const float k_0100[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
	const __m128 projPt = _mm_movelh_ps(
		projRaw, _mm_load_ps(k_0100));

	ALIGN16 const float k_1100[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
	__m128 boundsMax = _mm_movelh_ps(
		_mm_loadu_ps((const float*)a6), _mm_load_ps(k_1100));
	_mm_storeu_ps((float*)a6, boundsMax);

	const __m128 boundsMin = *a5;

	const __m128 kHalf = _mm_set1_ps(0.5f);
	const __m128 center = _mm_mul_ps(
		_mm_add_ps(boundsMin, boundsMax), kHalf);
	const __m128 halfSize = _mm_mul_ps(
		_mm_sub_ps(boundsMax, boundsMin), kHalf);

	const __m128 offset = _mm_sub_ps(projPt, center);

	const __m128 kSignMask = _mm_set1_ps(-0.0f);
	const __m128 kFltMin = _mm_set1_ps(1.17549435e-38f);
	const __m128 absOffset = _mm_max_ps(
		_mm_andnot_ps(kSignMask, offset), kFltMin);

	const __m128 ratio = _mm_div_ps(
		_mm_min_ps(absOffset, halfSize), absOffset);

	__m128 r2 = _mm_min_ps(
		_mm_castsi128_ps(_mm_shuffle_epi32(
			_mm_castps_si128(ratio), 0x4E)),
		ratio);
	const __m128 minRatio = _mm_min_ps(r2,
		_mm_castsi128_ps(_mm_shuffle_epi32(
			_mm_castps_si128(r2), 0xB1)));

	const __m128 clamped = _mm_add_ps(
		_mm_mul_ps(minRatio, offset), center);

	const __m128 clampDelta = _mm_sub_ps(clamped, projPt);
	const __m128 deltaSq = _mm_mul_ps(clampDelta, clampDelta);
	const __m128 sum1 = _mm_add_ps(
		_mm_castsi128_ps(_mm_shuffle_epi32(
			_mm_castps_si128(deltaSq), 0xB1)),
		deltaSq);
	const __m128 totalDist = _mm_add_ps(
		_mm_castsi128_ps(_mm_shuffle_epi32(
			_mm_castps_si128(sum1), 0x1B)),
		sum1);

	constexpr float kEpsilon = 1.1920929e-7f;

	if (_mm_cvtss_f32(totalDist) <= kEpsilon)
	{
		return _mm_set_ps(0.0f, 0.0f,
			clamped.m128_f32[1], clamped.m128_f32[0]);
	}

	float adjustedX = clamped.m128_f32[0];
	float adjustedY = clamped.m128_f32[1];

	const float boundsMinX = boundsMin.m128_f32[0];
	const float boundsMinY = boundsMin.m128_f32[1];
	const float boundsMaxX = boundsMax.m128_f32[0];
	const float boundsMaxY = boundsMax.m128_f32[1];
	const float clampDeltaX = clampDelta.m128_f32[0];
	const float clampDeltaY = clampDelta.m128_f32[1];

	if (distance > 0.0f && adjustedY > 0.68000001f)
	{
		adjustedX = (adjustedX >= 0.5f) ? boundsMaxX : boundsMinX;
		adjustedY = 0.68000001f;
	}

	float edgeIndicator;

	if (fabsf(adjustedY - boundsMinY) < kEpsilon
		|| fabsf(adjustedY - boundsMaxY) < kEpsilon)
	{
		edgeIndicator = (clampDeltaY > 0.0f) ? 0.0f : 2.0f;
	}
	else
	{
		edgeIndicator = (clampDeltaX > 0.0f) ? 1.0f : 3.0f;
	}

	return _mm_set_ps(1.0f, edgeIndicator, adjustedY, adjustedX);
}

//-----------------------------------------------------------------------------
// Index 37: AlignmentLookup
//-----------------------------------------------------------------------------
static float __fastcall Rui_AlignmentLookup(int a1)
{
	switch (a1)
	{
	case 1: return -0.25f;
	case 2: return 0.5f;
	case 3: return 0.25f;
	default: return 0.0f;
	}
}

//-----------------------------------------------------------------------------
// Index 12: LocalizeNumber
//-----------------------------------------------------------------------------
static const char* Rui_LocalizeNumber(__int64 context, const char* numberStr, bool /*uppercase*/)
{
	if (!numberStr || !context)
		return "0";

	return numberStr;
}

//-----------------------------------------------------------------------------
// Index 13: FormatAndLocalizeNumber
//-----------------------------------------------------------------------------
static const char* Rui_FormatAndLocalizeNumber(__int64 context, const char* fmt, float number, bool /*asInt*/, bool /*uppercase*/)
{
	if (s_originalVtableFuncs[11])
	{
		typedef const char* (*FormatNumber_t)(__int64, const char*, float);
		return reinterpret_cast<FormatNumber_t>(s_originalVtableFuncs[11])(context, fmt, number);
	}
	return "0";
}

//-----------------------------------------------------------------------------
// Index 17: Atan2Norm
//-----------------------------------------------------------------------------
static float Rui_Atan2Norm(float y, float x)
{
	constexpr float TWO_PI = 6.28318530717958647693f;
	return atan2f(y, x) / TWO_PI;
}

//=============================================================================
// GENERIC VTABLE WRAPPERS
//=============================================================================

typedef __int64 (*GenericVtableFunc_t)(__int64, __int64, __int64, __int64, __int64, __int64, __int64, __int64);

#define VTABLE_WRAPPER(idx) \
static __int64 VtableWrapper_##idx(__int64 a1, __int64 a2, __int64 a3, __int64 a4, __int64 a5, __int64 a6, __int64 a7, __int64 a8) \
{ \
	return reinterpret_cast<GenericVtableFunc_t>(s_originalVtableFuncs[idx])(a1, a2, a3, a4, a5, a6, a7, a8); \
}

#define VTABLE_WRAPPER_DEF_VALIDATED(idx) \
static __int64 VtableWrapper_##idx(__int64 a1, __int64 a2, __int64 a3, __int64 a4, __int64 a5, __int64 a6, __int64 a7, __int64 a8) \
{ \
	if (a1 > 0x10000) \
	{ \
		uintptr_t defPtr = *reinterpret_cast<uintptr_t*>(a1); \
		if (defPtr != 0 && static_cast<uint32_t>(defPtr) == 0) \
			return 0; \
	} \
	return reinterpret_cast<GenericVtableFunc_t>(s_originalVtableFuncs[idx])(a1, a2, a3, a4, a5, a6, a7, a8); \
}

VTABLE_WRAPPER(0)
VTABLE_WRAPPER(1)
VTABLE_WRAPPER(5)
VTABLE_WRAPPER(6)
VTABLE_WRAPPER_DEF_VALIDATED(7)
VTABLE_WRAPPER(8)
VTABLE_WRAPPER(9)
VTABLE_WRAPPER(10)
VTABLE_WRAPPER(11)
VTABLE_WRAPPER(12)
VTABLE_WRAPPER(13)
VTABLE_WRAPPER(14)
VTABLE_WRAPPER(15)
VTABLE_WRAPPER_DEF_VALIDATED(16)
VTABLE_WRAPPER_DEF_VALIDATED(17)
VTABLE_WRAPPER(19)
VTABLE_WRAPPER(20)
VTABLE_WRAPPER(21)
VTABLE_WRAPPER(22)
VTABLE_WRAPPER(23)
VTABLE_WRAPPER(24)
VTABLE_WRAPPER(25)
VTABLE_WRAPPER(26)
VTABLE_WRAPPER(27)

//=============================================================================
// DRAW FUNCTION WRAPPERS
//=============================================================================

typedef __int64 (*DrawFunc_t)(__int64, __int64, __int64, __int64, __int64, __int64);

// Draw index 0: RenderText
static __int64 Rui_DrawWrapper_RenderText(__int64 a1, __int64 a2, __int64 a3, __int64 a4, __int64 a5, __int64 a6)
{
	return reinterpret_cast<DrawFunc_t>(s_originalDrawFuncs[0])(a1, a2, a3, a4, a5, a6);
}

// Draw index 1: DrawImage
// Newer ui.dll changed the blur sentinel from 0x2000 to -4. The engine only
// recognizes 0x2000. Temporarily patch -4 to 0x2000 before calling native.
// Max data struct size observed: ~2048 bytes. Reject offsets beyond that.
static constexpr uint16_t RUI_MAX_DATASTRUCT_SIZE = 4096;

static __int64 Rui_DrawWrapper_DrawImage(__int64 a1, __int64 a2, __int64 a3, __int64 a4, __int64 a5, __int64 a6)
{
	const uint16_t primaryOff  = *reinterpret_cast<const uint16_t*>(a3 + 8);
	const uint16_t secondaryOff = *reinterpret_cast<const uint16_t*>(a3 + 10);

	if (primaryOff >= RUI_MAX_DATASTRUCT_SIZE || secondaryOff >= RUI_MAX_DATASTRUCT_SIZE)
		return reinterpret_cast<DrawFunc_t>(s_originalDrawFuncs[1])(a1, a2, a3, a4, a5, a6);

	int32_t* const primaryPtr   = reinterpret_cast<int32_t*>(a2 + 0x60 + primaryOff);
	int32_t* const secondaryPtr = reinterpret_cast<int32_t*>(a2 + 0x60 + secondaryOff);

	const int32_t origPrimary   = *primaryPtr;
	const int32_t origSecondary = *secondaryPtr;

	if (origPrimary == -4)
		*primaryPtr = 0x2000;
	if (origSecondary == -4)
		*secondaryPtr = 0x2000;

	const __int64 result = reinterpret_cast<DrawFunc_t>(s_originalDrawFuncs[1])(a1, a2, a3, a4, a5, a6);

	*primaryPtr   = origPrimary;
	*secondaryPtr = origSecondary;

	return result;
}

// Draw index 2: DrawFill (same blur sentinel fix)
static __int64 Rui_DrawWrapper_DrawFill(__int64 a1, __int64 a2, __int64 a3, __int64 a4, __int64 a5, __int64 a6)
{
	const uint16_t imageOff = *reinterpret_cast<const uint16_t*>(a3 + 8);

	if (imageOff >= RUI_MAX_DATASTRUCT_SIZE)
		return reinterpret_cast<DrawFunc_t>(s_originalDrawFuncs[2])(a1, a2, a3, a4, a5, a6);

	int32_t* const imagePtr = reinterpret_cast<int32_t*>(a2 + 0x60 + imageOff);
	const int32_t origValue = *imagePtr;

	if (origValue == -4)
		*imagePtr = 0x2000;

	const __int64 result = reinterpret_cast<DrawFunc_t>(s_originalDrawFuncs[2])(a1, a2, a3, a4, a5, a6);

	*imagePtr = origValue;

	return result;
}

// Draw index 3-5: DrawLine, DrawFont, DrawClip (pass-through)
static __int64 Rui_DrawWrapper_DrawLine(__int64 a1, __int64 a2, __int64 a3, __int64 a4, __int64 a5, __int64 a6)
{
	return reinterpret_cast<DrawFunc_t>(s_originalDrawFuncs[3])(a1, a2, a3, a4, a5, a6);
}

static __int64 Rui_DrawWrapper_DrawFont(__int64 a1, __int64 a2, __int64 a3, __int64 a4, __int64 a5, __int64 a6)
{
	return reinterpret_cast<DrawFunc_t>(s_originalDrawFuncs[4])(a1, a2, a3, a4, a5, a6);
}

static __int64 Rui_DrawWrapper_DrawClip(__int64 a1, __int64 a2, __int64 a3, __int64 a4, __int64 a5, __int64 a6)
{
	return reinterpret_cast<DrawFunc_t>(s_originalDrawFuncs[5])(a1, a2, a3, a4, a5, a6);
}


static void* AllocateNearAddress(uintptr_t targetAddr, size_t size)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	uintptr_t pageSize = si.dwAllocationGranularity;

	HMODULE hModule = GetModuleHandleA("r5apex.exe");
	uintptr_t moduleBase = hModule ? reinterpret_cast<uintptr_t>(hModule) : 0x140000000;

	uintptr_t minAddr = moduleBase;
	if (targetAddr > 0x70000000 && (targetAddr - 0x70000000) > minAddr)
		minAddr = targetAddr - 0x70000000;

	uintptr_t maxAddr = targetAddr + 0x70000000;
	minAddr = (minAddr + pageSize - 1) & ~(pageSize - 1);

	for (uintptr_t addr = (targetAddr & ~(pageSize - 1)) + pageSize; addr < maxAddr; addr += pageSize)
	{
		void* result = VirtualAlloc(reinterpret_cast<void*>(addr), size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (result)
			return result;
	}

	for (uintptr_t addr = targetAddr & ~(pageSize - 1); addr >= minAddr; addr -= pageSize)
	{
		void* result = VirtualAlloc(reinterpret_cast<void*>(addr), size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (result)
			return result;
	}

	return nullptr;
}

//=============================================================================
// CRASH PROTECTION: SetArg definition pointer null check
//
// Prevents crash when RUI definition pointer is freed/corrupt.
// Patch site: RVA 0x301869 in SetArgByName
//=============================================================================

static bool s_setArgPatchApplied = false;
static void* s_setArgCodeCave = nullptr;

constexpr uintptr_t SETARG_CRASH_RVA    = 0x301869;
constexpr uintptr_t SETARG_CMP_NEXT_RVA = 0x301871;
constexpr uintptr_t SETARG_RETURN0_RVA  = 0x30194A;

static void Rui_PatchSetArgNullCheck()
{
	if (s_setArgPatchApplied)
		return;

	HMODULE hModule = GetModuleHandleA("r5apex.exe");
	if (!hModule)
		return;

	uintptr_t moduleBase = reinterpret_cast<uintptr_t>(hModule);
	uintptr_t patchAddr = moduleBase + SETARG_CRASH_RVA;

	uint8_t* patchBytes = reinterpret_cast<uint8_t*>(patchAddr);
	if (patchBytes[0] != 0x49 || patchBytes[1] != 0x8B || patchBytes[2] != 0x1F ||
		patchBytes[3] != 0x66 || patchBytes[4] != 0x44 || patchBytes[5] != 0x3B ||
		patchBytes[6] != 0x63 || patchBytes[7] != 0x4E)
	{
		Warning(eDLL_T::RTECH, "RUI: SetArg null check - unexpected bytes at RVA 0x%X\n", SETARG_CRASH_RVA);
		return;
	}

	s_setArgCodeCave = AllocateNearAddress(patchAddr, 32);
	if (!s_setArgCodeCave)
	{
		Warning(eDLL_T::RTECH, "RUI: Failed to allocate code cave for SetArg null check\n");
		return;
	}

	uint8_t* cave = reinterpret_cast<uint8_t*>(s_setArgCodeCave);
	uintptr_t caveAddr = reinterpret_cast<uintptr_t>(cave);
	int off = 0;

	cave[off++] = 0x49; cave[off++] = 0x8B; cave[off++] = 0x1F;  // mov rbx, [r15]
	cave[off++] = 0x85; cave[off++] = 0xDB;  // test ebx, ebx
	cave[off++] = 0x74; cave[off++] = 0x0A;  // jz .return_zero

	cave[off++] = 0x66; cave[off++] = 0x44; cave[off++] = 0x3B;
	cave[off++] = 0x63; cave[off++] = 0x4E;  // cmp r12w, [rbx+4Eh]

	uintptr_t backTarget = moduleBase + SETARG_CMP_NEXT_RVA;
	int32_t backRel = static_cast<int32_t>(static_cast<int64_t>(backTarget) - static_cast<int64_t>(caveAddr + off + 5));
	cave[off++] = 0xE9;
	memcpy(&cave[off], &backRel, 4); off += 4;

	uintptr_t ret0Target = moduleBase + SETARG_RETURN0_RVA;
	int32_t ret0Rel = static_cast<int32_t>(static_cast<int64_t>(ret0Target) - static_cast<int64_t>(caveAddr + off + 5));
	cave[off++] = 0xE9;
	memcpy(&cave[off], &ret0Rel, 4); off += 4;

	DWORD oldProtect;
	VirtualProtect(cave, 32, PAGE_EXECUTE_READ, &oldProtect);

	int32_t jumpToCodeCave = static_cast<int32_t>(static_cast<int64_t>(caveAddr) - static_cast<int64_t>(patchAddr + 5));
	uint8_t jumpPatch[8] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90 };
	memcpy(&jumpPatch[1], &jumpToCodeCave, 4);

	DWORD oldProtect2;
	if (VirtualProtect(reinterpret_cast<void*>(patchAddr), 8, PAGE_EXECUTE_READWRITE, &oldProtect2))
	{
		memcpy(reinterpret_cast<void*>(patchAddr), jumpPatch, 8);
		VirtualProtect(reinterpret_cast<void*>(patchAddr), 8, oldProtect2, &oldProtect2);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(patchAddr), 8);
		s_setArgPatchApplied = true;
		DevMsg(eDLL_T::RTECH, "RUI: SetArg null check applied at RVA 0x%X\n", SETARG_CRASH_RVA);
	}
}

//=============================================================================
// CRASH PROTECTION: UpdateWidget definition pointer null check
//
// Patch site: RVA 0x2FC685
//=============================================================================

static bool s_updateWidgetPatchApplied = false;
static void* s_updateWidgetCodeCave = nullptr;

constexpr uintptr_t UPDATEWIDGET_PATCH_RVA = 0x2FC685;
constexpr uintptr_t UPDATEWIDGET_NEXT_RVA  = 0x2FC68D;

static void Rui_PatchUpdateWidgetNullCheck()
{
	if (s_updateWidgetPatchApplied)
		return;

	HMODULE hModule = GetModuleHandleA("r5apex.exe");
	if (!hModule) return;

	uintptr_t moduleBase = reinterpret_cast<uintptr_t>(hModule);
	uintptr_t patchAddr = moduleBase + UPDATEWIDGET_PATCH_RVA;

	uint8_t* patchBytes = reinterpret_cast<uint8_t*>(patchAddr);
	if (patchBytes[0] != 0x48 || patchBytes[1] != 0x8B || patchBytes[2] != 0x2A ||
		patchBytes[3] != 0x48 || patchBytes[4] != 0x89 || patchBytes[5] != 0x74 ||
		patchBytes[6] != 0x24 || patchBytes[7] != 0x58)
	{
		Warning(eDLL_T::RTECH, "RUI: UpdateWidget null check - unexpected bytes at RVA 0x%X\n", UPDATEWIDGET_PATCH_RVA);
		return;
	}

	s_updateWidgetCodeCave = AllocateNearAddress(patchAddr, 48);
	if (!s_updateWidgetCodeCave)
	{
		Warning(eDLL_T::RTECH, "RUI: Failed to allocate code cave for UpdateWidget null check\n");
		return;
	}

	uint8_t* cave = reinterpret_cast<uint8_t*>(s_updateWidgetCodeCave);
	uintptr_t caveAddr = reinterpret_cast<uintptr_t>(cave);
	int off = 0;

	cave[off++] = 0x48; cave[off++] = 0x8B; cave[off++] = 0x2A;  // mov rbp, [rdx]
	cave[off++] = 0x85; cave[off++] = 0xED;  // test ebp, ebp
	cave[off++] = 0x74; cave[off++] = 0x0A;  // jz .return_zero

	cave[off++] = 0x48; cave[off++] = 0x89; cave[off++] = 0x74;
	cave[off++] = 0x24; cave[off++] = 0x58;  // mov [rsp+58h], rsi

	uintptr_t backTarget = moduleBase + UPDATEWIDGET_NEXT_RVA;
	int32_t backRel = static_cast<int32_t>(static_cast<int64_t>(backTarget) - static_cast<int64_t>(caveAddr + off + 5));
	cave[off++] = 0xE9;
	memcpy(&cave[off], &backRel, 4); off += 4;

	// .return_zero: restore rbp, xor eax, add rsp, pop rbx, ret
	cave[off++] = 0x48; cave[off++] = 0x8B; cave[off++] = 0x6C; cave[off++] = 0x24; cave[off++] = 0x50;
	cave[off++] = 0x33; cave[off++] = 0xC0;
	cave[off++] = 0x48; cave[off++] = 0x83; cave[off++] = 0xC4; cave[off++] = 0x40;
	cave[off++] = 0x5B;
	cave[off++] = 0xC3;

	DWORD oldProtect;
	VirtualProtect(cave, 48, PAGE_EXECUTE_READ, &oldProtect);

	int32_t jumpToCodeCave = static_cast<int32_t>(static_cast<int64_t>(caveAddr) - static_cast<int64_t>(patchAddr + 5));
	uint8_t jumpPatch[8] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90 };
	memcpy(&jumpPatch[1], &jumpToCodeCave, 4);

	DWORD oldProtect2;
	if (VirtualProtect(reinterpret_cast<void*>(patchAddr), 8, PAGE_EXECUTE_READWRITE, &oldProtect2))
	{
		memcpy(reinterpret_cast<void*>(patchAddr), jumpPatch, 8);
		VirtualProtect(reinterpret_cast<void*>(patchAddr), 8, oldProtect2, &oldProtect2);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(patchAddr), 8);
		s_updateWidgetPatchApplied = true;
		DevMsg(eDLL_T::RTECH, "RUI: UpdateWidget null check applied at RVA 0x%X\n", UPDATEWIDGET_PATCH_RVA);
	}
}

//=============================================================================
// CRASH PROTECTION: ProcessAllWidgets null instance check
//
// Patch site: RVA 0x2FD383
//=============================================================================

static bool s_processWidgetsNullInstApplied = false;
static void* s_processWidgetsNullInstCave = nullptr;

constexpr uintptr_t PROCESSWIDGETS_INST_PATCH_RVA    = 0x2FD383;
constexpr uintptr_t PROCESSWIDGETS_INST_NEXT_RVA     = 0x2FD38C;
constexpr uintptr_t PROCESSWIDGETS_LOOP_CONTINUE_RVA = 0x2FD49B;

static void Rui_PatchProcessWidgetsNullInstance()
{
	if (s_processWidgetsNullInstApplied)
		return;

	HMODULE hModule = GetModuleHandleA("r5apex.exe");
	if (!hModule) return;

	uintptr_t moduleBase = reinterpret_cast<uintptr_t>(hModule);
	uintptr_t patchAddr = moduleBase + PROCESSWIDGETS_INST_PATCH_RVA;

	uint8_t* patchBytes = reinterpret_cast<uint8_t*>(patchAddr);
	if (patchBytes[0] != 0x48 || patchBytes[1] != 0x8B || patchBytes[2] != 0x74 ||
		patchBytes[3] != 0xC5 || patchBytes[4] != 0x18 || patchBytes[5] != 0x40 ||
		patchBytes[6] != 0x38 || patchBytes[7] != 0x7E || patchBytes[8] != 0x4A)
	{
		Warning(eDLL_T::RTECH, "RUI: ProcessWidgets null instance - unexpected bytes at RVA 0x%X\n", PROCESSWIDGETS_INST_PATCH_RVA);
		return;
	}

	s_processWidgetsNullInstCave = AllocateNearAddress(patchAddr, 32);
	if (!s_processWidgetsNullInstCave)
	{
		Warning(eDLL_T::RTECH, "RUI: Failed to allocate code cave for ProcessWidgets null instance\n");
		return;
	}

	uint8_t* cave = reinterpret_cast<uint8_t*>(s_processWidgetsNullInstCave);
	uintptr_t caveAddr = reinterpret_cast<uintptr_t>(cave);
	int off = 0;

	cave[off++] = 0x48; cave[off++] = 0x8B; cave[off++] = 0x74;
	cave[off++] = 0xC5; cave[off++] = 0x18;  // mov rsi, [rbp+rax*8+18h]
	cave[off++] = 0x48; cave[off++] = 0x85; cave[off++] = 0xF6;  // test rsi, rsi
	cave[off++] = 0x74; cave[off++] = 0x09;  // jz .skip_widget

	cave[off++] = 0x40; cave[off++] = 0x38; cave[off++] = 0x7E; cave[off++] = 0x4A;  // cmp [rsi+4Ah], dil

	uintptr_t backTarget = moduleBase + PROCESSWIDGETS_INST_NEXT_RVA;
	int32_t backRel = static_cast<int32_t>(static_cast<int64_t>(backTarget) - static_cast<int64_t>(caveAddr + off + 5));
	cave[off++] = 0xE9;
	memcpy(&cave[off], &backRel, 4); off += 4;

	uintptr_t skipTarget = moduleBase + PROCESSWIDGETS_LOOP_CONTINUE_RVA;
	int32_t skipRel = static_cast<int32_t>(static_cast<int64_t>(skipTarget) - static_cast<int64_t>(caveAddr + off + 5));
	cave[off++] = 0xE9;
	memcpy(&cave[off], &skipRel, 4); off += 4;

	DWORD oldProtect;
	VirtualProtect(cave, 32, PAGE_EXECUTE_READ, &oldProtect);

	int32_t jumpToCodeCave = static_cast<int32_t>(static_cast<int64_t>(caveAddr) - static_cast<int64_t>(patchAddr + 5));
	uint8_t jumpPatch[9] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 };
	memcpy(&jumpPatch[1], &jumpToCodeCave, 4);

	DWORD oldProtect2;
	if (VirtualProtect(reinterpret_cast<void*>(patchAddr), 9, PAGE_EXECUTE_READWRITE, &oldProtect2))
	{
		memcpy(reinterpret_cast<void*>(patchAddr), jumpPatch, 9);
		VirtualProtect(reinterpret_cast<void*>(patchAddr), 9, oldProtect2, &oldProtect2);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(patchAddr), 9);
		s_processWidgetsNullInstApplied = true;
		DevMsg(eDLL_T::RTECH, "RUI: ProcessWidgets null instance check applied at RVA 0x%X\n", PROCESSWIDGETS_INST_PATCH_RVA);
	}
}

//=============================================================================
// CRASH PROTECTION: ProcessAllWidgets definition pointer check
//
// Patch site: RVA 0x2FD3CB
//=============================================================================

static bool s_processWidgetsDefCheckApplied = false;
static void* s_processWidgetsDefCheckCave = nullptr;

constexpr uintptr_t PROCESSWIDGETS_DEF_PATCH_RVA = 0x2FD3CB;
constexpr uintptr_t PROCESSWIDGETS_DEF_NEXT_RVA  = 0x2FD3D3;

static void Rui_PatchProcessWidgetsDefCheck()
{
	if (s_processWidgetsDefCheckApplied)
		return;

	HMODULE hModule = GetModuleHandleA("r5apex.exe");
	if (!hModule) return;

	uintptr_t moduleBase = reinterpret_cast<uintptr_t>(hModule);
	uintptr_t patchAddr = moduleBase + PROCESSWIDGETS_DEF_PATCH_RVA;

	uint8_t* patchBytes = reinterpret_cast<uint8_t*>(patchAddr);
	if (patchBytes[0] != 0x48 || patchBytes[1] != 0x8B || patchBytes[2] != 0x06 ||
		patchBytes[3] != 0x4C || patchBytes[4] != 0x8D || patchBytes[5] != 0x44 ||
		patchBytes[6] != 0x24 || patchBytes[7] != 0x60)
	{
		Warning(eDLL_T::RTECH, "RUI: ProcessWidgets def check - unexpected bytes at RVA 0x%X\n", PROCESSWIDGETS_DEF_PATCH_RVA);
		return;
	}

	s_processWidgetsDefCheckCave = AllocateNearAddress(patchAddr, 32);
	if (!s_processWidgetsDefCheckCave)
	{
		Warning(eDLL_T::RTECH, "RUI: Failed to allocate code cave for ProcessWidgets def check\n");
		return;
	}

	uint8_t* cave = reinterpret_cast<uint8_t*>(s_processWidgetsDefCheckCave);
	uintptr_t caveAddr = reinterpret_cast<uintptr_t>(cave);
	int off = 0;

	cave[off++] = 0x48; cave[off++] = 0x8B; cave[off++] = 0x06;  // mov rax, [rsi]
	cave[off++] = 0x85; cave[off++] = 0xC0;  // test eax, eax
	cave[off++] = 0x74; cave[off++] = 0x0A;  // jz .skip

	cave[off++] = 0x4C; cave[off++] = 0x8D; cave[off++] = 0x44;
	cave[off++] = 0x24; cave[off++] = 0x60;  // lea r8, [rsp+60h]

	uintptr_t backTarget = moduleBase + PROCESSWIDGETS_DEF_NEXT_RVA;
	int32_t backRel = static_cast<int32_t>(static_cast<int64_t>(backTarget) - static_cast<int64_t>(caveAddr + off + 5));
	cave[off++] = 0xE9;
	memcpy(&cave[off], &backRel, 4); off += 4;

	uintptr_t skipTarget = moduleBase + PROCESSWIDGETS_LOOP_CONTINUE_RVA;
	int32_t skipRel = static_cast<int32_t>(static_cast<int64_t>(skipTarget) - static_cast<int64_t>(caveAddr + off + 5));
	cave[off++] = 0xE9;
	memcpy(&cave[off], &skipRel, 4); off += 4;

	DWORD oldProtect;
	VirtualProtect(cave, 32, PAGE_EXECUTE_READ, &oldProtect);

	int32_t jumpToCodeCave = static_cast<int32_t>(static_cast<int64_t>(caveAddr) - static_cast<int64_t>(patchAddr + 5));
	uint8_t jumpPatch[8] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90 };
	memcpy(&jumpPatch[1], &jumpToCodeCave, 4);

	DWORD oldProtect2;
	if (VirtualProtect(reinterpret_cast<void*>(patchAddr), 8, PAGE_EXECUTE_READWRITE, &oldProtect2))
	{
		memcpy(reinterpret_cast<void*>(patchAddr), jumpPatch, 8);
		VirtualProtect(reinterpret_cast<void*>(patchAddr), 8, oldProtect2, &oldProtect2);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(patchAddr), 8);
		s_processWidgetsDefCheckApplied = true;
		DevMsg(eDLL_T::RTECH, "RUI: ProcessWidgets def check applied at RVA 0x%X\n", PROCESSWIDGETS_DEF_PATCH_RVA);
	}
}

//=============================================================================
// CRASH PROTECTION: DestroyWidget definition array cleanup
//
// Nulls stale instance pointers in the per-definition array before HeapFree.
// Patch site: RVA 0x71CB60
//=============================================================================

static bool s_destroyWidgetCleanupApplied = false;
static void* s_destroyWidgetCleanupCave = nullptr;

constexpr uintptr_t DESTROYWIDGET_PATCH_RVA     = 0x71CB60;
constexpr uintptr_t DESTROYWIDGET_HEAPFREE_RVA  = 0x71CB6A;
constexpr uintptr_t DESTROYWIDGET_ALLOCATOR_RVA = 0x171C180;

static void Rui_PatchDestroyWidgetCleanup()
{
	if (s_destroyWidgetCleanupApplied)
		return;

	HMODULE hModule = GetModuleHandleA("r5apex.exe");
	if (!hModule) return;

	uintptr_t moduleBase = reinterpret_cast<uintptr_t>(hModule);
	uintptr_t patchAddr = moduleBase + DESTROYWIDGET_PATCH_RVA;

	uint8_t* patchBytes = reinterpret_cast<uint8_t*>(patchAddr);
	if (patchBytes[0] != 0x48 || patchBytes[1] != 0x8B || patchBytes[2] != 0xD7 ||
		patchBytes[3] != 0x48 || patchBytes[4] != 0x8D || patchBytes[5] != 0x0D)
	{
		Warning(eDLL_T::RTECH, "RUI: DestroyWidget cleanup - unexpected bytes at RVA 0x%X\n", DESTROYWIDGET_PATCH_RVA);
		return;
	}

	s_destroyWidgetCleanupCave = AllocateNearAddress(patchAddr, 64);
	if (!s_destroyWidgetCleanupCave)
	{
		Warning(eDLL_T::RTECH, "RUI: Failed to allocate code cave for DestroyWidget cleanup\n");
		return;
	}

	uintptr_t allocatorAddr = moduleBase + DESTROYWIDGET_ALLOCATOR_RVA;

	uint8_t* cave = reinterpret_cast<uint8_t*>(s_destroyWidgetCleanupCave);
	uintptr_t caveAddr = reinterpret_cast<uintptr_t>(cave);
	int off = 0;

	// Scan definition array for the instance being freed
	cave[off++] = 0x0F; cave[off++] = 0xB7; cave[off++] = 0x4D; cave[off++] = 0x16;  // movzx ecx, word ptr [rbp+16h]
	cave[off++] = 0x48; cave[off++] = 0x8D; cave[off++] = 0x55; cave[off++] = 0x18;  // lea rdx, [rbp+18h]
	cave[off++] = 0x85; cave[off++] = 0xC9;  // test ecx, ecx
	cave[off++] = 0x74; cave[off++] = 0x16;  // jz .done_scan
	// .loop:
	cave[off++] = 0x48; cave[off++] = 0x39; cave[off++] = 0x3A;  // cmp [rdx], rdi
	cave[off++] = 0x75; cave[off++] = 0x09;  // jne .next
	cave[off++] = 0x48; cave[off++] = 0xC7; cave[off++] = 0x02;
	cave[off++] = 0x00; cave[off++] = 0x00; cave[off++] = 0x00; cave[off++] = 0x00;  // mov qword [rdx], 0
	cave[off++] = 0xEB; cave[off++] = 0x08;  // jmp .done_scan
	// .next:
	cave[off++] = 0x48; cave[off++] = 0x83; cave[off++] = 0xC2; cave[off++] = 0x08;  // add rdx, 8
	cave[off++] = 0xFF; cave[off++] = 0xC9;  // dec ecx
	cave[off++] = 0x75; cave[off++] = 0xEA;  // jnz .loop
	// .done_scan:
	cave[off++] = 0x48; cave[off++] = 0x8B; cave[off++] = 0xD7;  // mov rdx, rdi
	cave[off++] = 0x48; cave[off++] = 0xB9;  // mov rcx, imm64
	memcpy(&cave[off], &allocatorAddr, 8); off += 8;

	uintptr_t backTarget = moduleBase + DESTROYWIDGET_HEAPFREE_RVA;
	int32_t backRel = static_cast<int32_t>(static_cast<int64_t>(backTarget) - static_cast<int64_t>(caveAddr + off + 5));
	cave[off++] = 0xE9;
	memcpy(&cave[off], &backRel, 4); off += 4;

	DWORD oldProtect;
	VirtualProtect(cave, 64, PAGE_EXECUTE_READ, &oldProtect);

	int32_t jumpToCodeCave = static_cast<int32_t>(static_cast<int64_t>(caveAddr) - static_cast<int64_t>(patchAddr + 5));
	uint8_t jumpPatch[10] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90 };
	memcpy(&jumpPatch[1], &jumpToCodeCave, 4);

	DWORD oldProtect2;
	if (VirtualProtect(reinterpret_cast<void*>(patchAddr), 10, PAGE_EXECUTE_READWRITE, &oldProtect2))
	{
		memcpy(reinterpret_cast<void*>(patchAddr), jumpPatch, 10);
		VirtualProtect(reinterpret_cast<void*>(patchAddr), 10, oldProtect2, &oldProtect2);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(patchAddr), 10);
		s_destroyWidgetCleanupApplied = true;
		DevMsg(eDLL_T::RTECH, "RUI: DestroyWidget array cleanup applied at RVA 0x%X\n", DESTROYWIDGET_PATCH_RVA);
	}
}

//=============================================================================
// CRASH PROTECTION: Post-callback definition revalidation
//
// After ruiFunc callback, the definition may have been freed during execution.
// Patch site: RVA 0x2FC830
//=============================================================================

static bool s_postCallbackPatchApplied = false;
static void* s_postCallbackCodeCave = nullptr;

constexpr uintptr_t POSTCB_PATCH_RVA    = 0x2FC830;
constexpr uintptr_t POSTCB_NEXT_RVA     = 0x2FC83A;
constexpr uintptr_t XMM5_CONSTANT_RVA   = 0x1312DC0;

static void Rui_PatchPostCallbackNullCheck()
{
	if (s_postCallbackPatchApplied)
		return;

	HMODULE hModule = GetModuleHandleA("r5apex.exe");
	if (!hModule) return;

	uintptr_t moduleBase = reinterpret_cast<uintptr_t>(hModule);
	uintptr_t patchAddr = moduleBase + POSTCB_PATCH_RVA;

	uint8_t* patchBytes = reinterpret_cast<uint8_t*>(patchAddr);
	if (patchBytes[0] != 0x48 || patchBytes[1] != 0x8B || patchBytes[2] != 0x03 ||
		patchBytes[3] != 0x0F || patchBytes[4] != 0x28 || patchBytes[5] != 0x2D)
	{
		Warning(eDLL_T::RTECH, "RUI: Post-callback null check - unexpected bytes at RVA 0x%X\n", POSTCB_PATCH_RVA);
		return;
	}

	s_postCallbackCodeCave = AllocateNearAddress(patchAddr, 64);
	if (!s_postCallbackCodeCave)
	{
		Warning(eDLL_T::RTECH, "RUI: Failed to allocate code cave for post-callback null check\n");
		return;
	}

	uint8_t* cave = reinterpret_cast<uint8_t*>(s_postCallbackCodeCave);
	uintptr_t caveAddr = reinterpret_cast<uintptr_t>(cave);
	int off = 0;

	cave[off++] = 0x48; cave[off++] = 0x8B; cave[off++] = 0x03;  // mov rax, [rbx]
	cave[off++] = 0x85; cave[off++] = 0xC0;  // test eax, eax
	cave[off++] = 0x74; cave[off++] = 0x0C;  // jz .corrupt

	uintptr_t xmm5Target = moduleBase + XMM5_CONSTANT_RVA;
	int32_t xmm5Disp = static_cast<int32_t>(static_cast<int64_t>(xmm5Target) - static_cast<int64_t>(caveAddr + off + 7));
	cave[off++] = 0x0F; cave[off++] = 0x28; cave[off++] = 0x2D;  // movaps xmm5, [rip+disp32]
	memcpy(&cave[off], &xmm5Disp, 4); off += 4;

	uintptr_t backTarget = moduleBase + POSTCB_NEXT_RVA;
	int32_t backRel = static_cast<int32_t>(static_cast<int64_t>(backTarget) - static_cast<int64_t>(caveAddr + off + 5));
	cave[off++] = 0xE9;
	memcpy(&cave[off], &backRel, 4); off += 4;

	// .corrupt: restore non-volatiles and return 0
	cave[off++] = 0x0F; cave[off++] = 0x28; cave[off++] = 0x74; cave[off++] = 0x24; cave[off++] = 0x30;  // movaps xmm6, [rsp+30h]
	cave[off++] = 0x48; cave[off++] = 0x8B; cave[off++] = 0x7C; cave[off++] = 0x24; cave[off++] = 0x60;  // mov rdi, [rsp+60h]
	cave[off++] = 0x48; cave[off++] = 0x8B; cave[off++] = 0x6C; cave[off++] = 0x24; cave[off++] = 0x50;  // mov rbp, [rsp+50h]
	cave[off++] = 0x32; cave[off++] = 0xC0;  // xor al, al
	cave[off++] = 0x48; cave[off++] = 0x83; cave[off++] = 0xC4; cave[off++] = 0x40;  // add rsp, 40h
	cave[off++] = 0x5B;  // pop rbx
	cave[off++] = 0xC3;  // ret

	DWORD oldProtect;
	VirtualProtect(cave, 64, PAGE_EXECUTE_READ, &oldProtect);

	int32_t jumpToCodeCave = static_cast<int32_t>(static_cast<int64_t>(caveAddr) - static_cast<int64_t>(patchAddr + 5));
	uint8_t jumpPatch[10] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90 };
	memcpy(&jumpPatch[1], &jumpToCodeCave, 4);

	DWORD oldProtect2;
	if (VirtualProtect(reinterpret_cast<void*>(patchAddr), 10, PAGE_EXECUTE_READWRITE, &oldProtect2))
	{
		memcpy(reinterpret_cast<void*>(patchAddr), jumpPatch, 10);
		VirtualProtect(reinterpret_cast<void*>(patchAddr), 10, oldProtect2, &oldProtect2);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(patchAddr), 10);
		s_postCallbackPatchApplied = true;
		DevMsg(eDLL_T::RTECH, "RUI: Post-callback null check applied at RVA 0x%X\n", POSTCB_PATCH_RVA);
	}
}

//=============================================================================
// CRASH PROTECTION: VtableFunc19 (GetTransformSize) null check
//
// Patch site: RVA 0x2FBDA0
//=============================================================================

static bool s_vtableFunc19PatchApplied = false;
static void* s_vtableFunc19CodeCave = nullptr;

constexpr uintptr_t VTFUNC19_PATCH_RVA = 0x2FBDA0;
constexpr uintptr_t VTFUNC19_NEXT_RVA  = 0x2FBDA8;

static void Rui_PatchVtableFunc19NullCheck()
{
	if (s_vtableFunc19PatchApplied)
		return;

	HMODULE hModule = GetModuleHandleA("r5apex.exe");
	if (!hModule) return;

	uintptr_t moduleBase = reinterpret_cast<uintptr_t>(hModule);
	uintptr_t patchAddr = moduleBase + VTFUNC19_PATCH_RVA;

	uint8_t* patchBytes = reinterpret_cast<uint8_t*>(patchAddr);
	if (patchBytes[0] != 0x48 || patchBytes[1] != 0x8B || patchBytes[2] != 0x01 ||
		patchBytes[3] != 0xF3 || patchBytes[4] != 0x0F || patchBytes[5] != 0x7E ||
		patchBytes[6] != 0x41 || patchBytes[7] != 0x10)
	{
		Warning(eDLL_T::RTECH, "RUI: VtableFunc19 null check - unexpected bytes at RVA 0x%X\n", VTFUNC19_PATCH_RVA);
		return;
	}

	s_vtableFunc19CodeCave = AllocateNearAddress(patchAddr, 32);
	if (!s_vtableFunc19CodeCave)
	{
		Warning(eDLL_T::RTECH, "RUI: Failed to allocate code cave for VtableFunc19 null check\n");
		return;
	}

	uint8_t* cave = reinterpret_cast<uint8_t*>(s_vtableFunc19CodeCave);
	uintptr_t caveAddr = reinterpret_cast<uintptr_t>(cave);
	int off = 0;

	cave[off++] = 0x48; cave[off++] = 0x8B; cave[off++] = 0x01;  // mov rax, [rcx]
	cave[off++] = 0x85; cave[off++] = 0xC0;  // test eax, eax
	cave[off++] = 0x74; cave[off++] = 0x0A;  // jz .bail
	cave[off++] = 0xF3; cave[off++] = 0x0F; cave[off++] = 0x7E; cave[off++] = 0x41; cave[off++] = 0x10;  // movq xmm0, [rcx+10h]

	uintptr_t backTarget = moduleBase + VTFUNC19_NEXT_RVA;
	int32_t backRel = static_cast<int32_t>(static_cast<int64_t>(backTarget) - static_cast<int64_t>(caveAddr + off + 5));
	cave[off++] = 0xE9;
	memcpy(&cave[off], &backRel, 4); off += 4;

	// .bail: xorps xmm0, xmm0; ret
	cave[off++] = 0x0F; cave[off++] = 0x57; cave[off++] = 0xC0;
	cave[off++] = 0xC3;

	DWORD oldProtect;
	VirtualProtect(cave, 32, PAGE_EXECUTE_READ, &oldProtect);

	int32_t jumpToCodeCave = static_cast<int32_t>(static_cast<int64_t>(caveAddr) - static_cast<int64_t>(patchAddr + 5));
	uint8_t jumpPatch[8] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90 };
	memcpy(&jumpPatch[1], &jumpToCodeCave, 4);

	DWORD oldProtect2;
	if (VirtualProtect(reinterpret_cast<void*>(patchAddr), 8, PAGE_EXECUTE_READWRITE, &oldProtect2))
	{
		memcpy(reinterpret_cast<void*>(patchAddr), jumpPatch, 8);
		VirtualProtect(reinterpret_cast<void*>(patchAddr), 8, oldProtect2, &oldProtect2);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(patchAddr), 8);
		s_vtableFunc19PatchApplied = true;
		DevMsg(eDLL_T::RTECH, "RUI: VtableFunc19 null check applied at RVA 0x%X\n", VTFUNC19_PATCH_RVA);
	}
}

//=============================================================================
// CRASH PROTECTION: VtableFunc20 (GetRenderJobDimensions) null check
//
// Patch site: RVA 0x2FBDD0
//=============================================================================

static bool s_vtableFunc20PatchApplied = false;
static void* s_vtableFunc20CodeCave = nullptr;

constexpr uintptr_t VTFUNC20_PATCH_RVA = 0x2FBDD0;
constexpr uintptr_t VTFUNC20_NEXT_RVA  = 0x2FBDDA;

static void Rui_PatchVtableFunc20NullCheck()
{
	if (s_vtableFunc20PatchApplied)
		return;

	HMODULE hModule = GetModuleHandleA("r5apex.exe");
	if (!hModule) return;

	uintptr_t moduleBase = reinterpret_cast<uintptr_t>(hModule);
	uintptr_t patchAddr = moduleBase + VTFUNC20_PATCH_RVA;

	uint8_t* patchBytes = reinterpret_cast<uint8_t*>(patchAddr);
	if (patchBytes[0] != 0x48 || patchBytes[1] != 0x8B || patchBytes[2] != 0x01 ||
		patchBytes[3] != 0x48 || patchBytes[4] != 0x63 || patchBytes[5] != 0xD2 ||
		patchBytes[6] != 0x48 || patchBytes[7] != 0x8B || patchBytes[8] != 0x40 ||
		patchBytes[9] != 0x58)
	{
		Warning(eDLL_T::RTECH, "RUI: VtableFunc20 null check - unexpected bytes at RVA 0x%X\n", VTFUNC20_PATCH_RVA);
		return;
	}

	s_vtableFunc20CodeCave = AllocateNearAddress(patchAddr, 32);
	if (!s_vtableFunc20CodeCave)
	{
		Warning(eDLL_T::RTECH, "RUI: Failed to allocate code cave for VtableFunc20 null check\n");
		return;
	}

	uint8_t* cave = reinterpret_cast<uint8_t*>(s_vtableFunc20CodeCave);
	uintptr_t caveAddr = reinterpret_cast<uintptr_t>(cave);
	int off = 0;

	cave[off++] = 0x48; cave[off++] = 0x8B; cave[off++] = 0x01;  // mov rax, [rcx]
	cave[off++] = 0x85; cave[off++] = 0xC0;  // test eax, eax
	cave[off++] = 0x74; cave[off++] = 0x0C;  // jz .bail
	cave[off++] = 0x48; cave[off++] = 0x63; cave[off++] = 0xD2;  // movsxd rdx, edx
	cave[off++] = 0x48; cave[off++] = 0x8B; cave[off++] = 0x40; cave[off++] = 0x58;  // mov rax, [rax+58h]

	uintptr_t backTarget = moduleBase + VTFUNC20_NEXT_RVA;
	int32_t backRel = static_cast<int32_t>(static_cast<int64_t>(backTarget) - static_cast<int64_t>(caveAddr + off + 5));
	cave[off++] = 0xE9;
	memcpy(&cave[off], &backRel, 4); off += 4;

	cave[off++] = 0x0F; cave[off++] = 0x57; cave[off++] = 0xC0;  // xorps xmm0, xmm0
	cave[off++] = 0xC3;  // ret

	DWORD oldProtect;
	VirtualProtect(cave, 32, PAGE_EXECUTE_READ, &oldProtect);

	int32_t jumpToCodeCave = static_cast<int32_t>(static_cast<int64_t>(caveAddr) - static_cast<int64_t>(patchAddr + 5));
	uint8_t jumpPatch[10] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90 };
	memcpy(&jumpPatch[1], &jumpToCodeCave, 4);

	DWORD oldProtect2;
	if (VirtualProtect(reinterpret_cast<void*>(patchAddr), 10, PAGE_EXECUTE_READWRITE, &oldProtect2))
	{
		memcpy(reinterpret_cast<void*>(patchAddr), jumpPatch, 10);
		VirtualProtect(reinterpret_cast<void*>(patchAddr), 10, oldProtect2, &oldProtect2);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(patchAddr), 10);
		s_vtableFunc20PatchApplied = true;
		DevMsg(eDLL_T::RTECH, "RUI: VtableFunc20 null check applied at RVA 0x%X\n", VTFUNC20_PATCH_RVA);
	}
}

//=============================================================================
// DrawImage TILING-LITE PATCH
//
// NOP out UV/rotation overrides in TILING mode to preserve atlas coordinates
// while keeping the identity transform fix for aspect ratio correction.
//=============================================================================

static bool s_tilingLitePatchApplied = false;

constexpr uintptr_t TILING_V21_OVERRIDE_RVA = 0x2F9A21;
constexpr uintptr_t TILING_V25_OVERRIDE_RVA = 0x2F9A29;
constexpr uintptr_t TILING_V15_OVERRIDE_RVA = 0x2F9A32;

static void Rui_PatchDrawImageTilingLite()
{
	if (s_tilingLitePatchApplied)
		return;

	HMODULE hModule = GetModuleHandleA("r5apex.exe");
	if (!hModule) return;

	uintptr_t moduleBase = reinterpret_cast<uintptr_t>(hModule);

	struct PatchSite { uintptr_t rva; uint8_t expected[4]; };
	PatchSite sites[] = {
		{ TILING_V21_OVERRIDE_RVA, { 0x45, 0x0F, 0x28, 0xFC } },
		{ TILING_V25_OVERRIDE_RVA, { 0x45, 0x0F, 0x57, 0xED } },
		{ TILING_V15_OVERRIDE_RVA, { 0x45, 0x0F, 0x57, 0xD2 } },
	};

	for (const auto& site : sites)
	{
		uint8_t* patchAddr = reinterpret_cast<uint8_t*>(moduleBase + site.rva);
		if (memcmp(patchAddr, site.expected, 4) != 0)
		{
			Warning(eDLL_T::RTECH, "RUI: TILING-lite patch - unexpected bytes at RVA 0x%X\n", site.rva);
			return;
		}
	}

	uint8_t* regionStart = reinterpret_cast<uint8_t*>(moduleBase + TILING_V21_OVERRIDE_RVA);
	SIZE_T regionSize = (TILING_V15_OVERRIDE_RVA + 4) - TILING_V21_OVERRIDE_RVA;

	DWORD oldProtect;
	if (!VirtualProtect(regionStart, regionSize, PAGE_EXECUTE_READWRITE, &oldProtect))
		return;

	for (const auto& site : sites)
		memset(reinterpret_cast<uint8_t*>(moduleBase + site.rva), 0x90, 4);

	VirtualProtect(regionStart, regionSize, oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), regionStart, regionSize);

	s_tilingLitePatchApplied = true;
	DevMsg(eDLL_T::RTECH, "RUI: TILING-lite patch applied\n");
}

//=============================================================================
// RUIFUNC STATE STRUCT SANITIZATION (C++ TRAMPOLINE)
//
// The engine doesn't populate certain state fields that newer ui.dll reads.
// A shadow buffer approach copies shared state, applies field remaps, calls
// ruiFunc with the shadow, then discards it — original state is never modified.
//=============================================================================

struct FieldRemap
{
	uint32_t srcOffset;
	uint32_t dstOffset;
};

static constexpr uint32_t s_phase1ZeroOffsets[] = {
	0xD0, 0xD4, 0xFC, 0x100,
};

static constexpr FieldRemap s_phase2Remaps[] = {
	{ 0x100, 0x104 }, { 0xD0,  0xD4  }, { 0xC4,  0xC8  }, { 0xBC,  0xC0  },
	{ 0xB0,  0xB4  }, { 0xAC,  0xB0  }, { 0xA8,  0xAC  }, { 0xA4,  0xA8  },
};

static constexpr size_t SHADOW_STATE_SIZE = 0x1000;

typedef void (*RuiFunc_t)(void*, void*, void*, char*);

static void Rui_RuiFuncTrampoline(
	void* api, void* state, void* instance, char* values,
	RuiFunc_t ruiFunc, uintptr_t headerPtr, uintptr_t contextPtr)
{
	uint8_t* s = reinterpret_cast<uint8_t*>(state);

	// Shadow buffer approach: copy, remap, call, discard
	alignas(16) uint8_t shadow[SHADOW_STATE_SIZE];
	memcpy(shadow, s, SHADOW_STATE_SIZE);

	for (uint32_t off : s_phase1ZeroOffsets)
		*reinterpret_cast<uint32_t*>(shadow + off) = 0;

	for (const auto& r : s_phase2Remaps)
		*reinterpret_cast<uint32_t*>(shadow + r.dstOffset) =
			*reinterpret_cast<uint32_t*>(shadow + r.srcOffset);

	ruiFunc(api, shadow, instance, values);

	// Fix compass_flat tiling byte for atlas-based RPaks
	if (values[56] == 0 || values[60] == 0)
	{
		const char* name = *reinterpret_cast<const char**>(headerPtr);
		if (name && (reinterpret_cast<uintptr_t>(name) & 0xFFFF000000000000ULL) == 0
			&& name[0] == 'c' && name[1] == 'o' && name[2] == 'm' && name[3] == 'p'
			&& name[4] == 'a' && name[5] == 's' && name[6] == 's' && name[7] == '_'
			&& name[8] == 'f' && name[9] == 'l' && name[10] == 'a' && name[11] == 't')
		{
			if (values[56] == 0) values[56] = 1;
			if (values[60] == 0) values[60] = 1;
		}
	}
}

static uint8_t s_ruiFuncOrigBytes[6] = { 0 };
static bool s_ruiFuncPatchApplied = false;
static void* s_ruiFuncCodeCave = nullptr;

constexpr uintptr_t RUIFUNC_CALL_PATCH_RVA = 0x2FC82A;

static void Rui_PatchRuiFuncStateOffset()
{
	if (s_ruiFuncPatchApplied)
		return;

	HMODULE hModule = GetModuleHandleA("r5apex.exe");
	if (!hModule) return;

	uintptr_t moduleBase = reinterpret_cast<uintptr_t>(hModule);
	uintptr_t patchAddr = moduleBase + RUIFUNC_CALL_PATCH_RVA;

	uint8_t* patchBytes = reinterpret_cast<uint8_t*>(patchAddr);
	if (patchBytes[0] != 0x48 || patchBytes[1] != 0x8B || patchBytes[2] != 0x17 ||
		patchBytes[3] != 0xFF || patchBytes[4] != 0x55 || patchBytes[5] != 0x68)
	{
		Warning(eDLL_T::RTECH, "RUI: ruiFunc trampoline - unexpected bytes at RVA 0x%X\n", RUIFUNC_CALL_PATCH_RVA);
		return;
	}

	memcpy(s_ruiFuncOrigBytes, patchBytes, 6);

	s_ruiFuncCodeCave = AllocateNearAddress(patchAddr, 64);
	if (!s_ruiFuncCodeCave)
	{
		Warning(eDLL_T::RTECH, "RUI: Failed to allocate code cave for ruiFunc trampoline\n");
		return;
	}

	uint8_t* cave = reinterpret_cast<uint8_t*>(s_ruiFuncCodeCave);
	uintptr_t caveAddr = reinterpret_cast<uintptr_t>(cave);
	uintptr_t jumpBackTarget = patchAddr + 6;
	int off = 0;

	cave[off++] = 0x48; cave[off++] = 0x8B; cave[off++] = 0x17;  // mov rdx, [rdi]
	cave[off++] = 0x48; cave[off++] = 0x83; cave[off++] = 0xEC; cave[off++] = 0x40;  // sub rsp, 40h
	cave[off++] = 0x48; cave[off++] = 0x8B; cave[off++] = 0x45; cave[off++] = 0x68;  // mov rax, [rbp+68h]
	cave[off++] = 0x48; cave[off++] = 0x89; cave[off++] = 0x44; cave[off++] = 0x24; cave[off++] = 0x20;  // mov [rsp+20h], rax
	cave[off++] = 0x48; cave[off++] = 0x89; cave[off++] = 0x6C; cave[off++] = 0x24; cave[off++] = 0x28;  // mov [rsp+28h], rbp
	cave[off++] = 0x48; cave[off++] = 0x89; cave[off++] = 0x5C; cave[off++] = 0x24; cave[off++] = 0x30;  // mov [rsp+30h], rbx

	cave[off++] = 0x48; cave[off++] = 0xB8;  // mov rax, imm64
	uintptr_t trampolineAddr = reinterpret_cast<uintptr_t>(&Rui_RuiFuncTrampoline);
	memcpy(&cave[off], &trampolineAddr, 8); off += 8;

	cave[off++] = 0xFF; cave[off++] = 0xD0;  // call rax
	cave[off++] = 0x48; cave[off++] = 0x83; cave[off++] = 0xC4; cave[off++] = 0x40;  // add rsp, 40h

	int32_t jumpBackRel = static_cast<int32_t>(static_cast<int64_t>(jumpBackTarget) - static_cast<int64_t>(caveAddr + off + 5));
	cave[off++] = 0xE9;
	memcpy(&cave[off], &jumpBackRel, 4); off += 4;

	DWORD oldProtect;
	VirtualProtect(cave, 64, PAGE_EXECUTE_READ, &oldProtect);

	int32_t jumpToCodeCave = static_cast<int32_t>(static_cast<int64_t>(caveAddr) - static_cast<int64_t>(patchAddr + 5));
	uint8_t jumpPatch[6] = { 0xE9, 0, 0, 0, 0, 0x90 };
	memcpy(&jumpPatch[1], &jumpToCodeCave, 4);

	DWORD oldProtect2;
	if (VirtualProtect(reinterpret_cast<void*>(patchAddr), 6, PAGE_EXECUTE_READWRITE, &oldProtect2))
	{
		memcpy(reinterpret_cast<void*>(patchAddr), jumpPatch, 6);
		VirtualProtect(reinterpret_cast<void*>(patchAddr), 6, oldProtect2, &oldProtect2);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(patchAddr), 6);
		s_ruiFuncPatchApplied = true;
		DevMsg(eDLL_T::RTECH, "RUI: ruiFunc C++ trampoline applied at RVA 0x%X\n", RUIFUNC_CALL_PATCH_RVA);
	}
}

//=============================================================================
// EXTENDED VTABLE INITIALIZATION (52-entry layout)
//=============================================================================

static void Rui_InitExtendedVtable()
{
	if (s_extendedVtableInitialized || !s_ruiApi)
		return;

	void** nativeVtable = reinterpret_cast<void**>(s_ruiApi);

	for (int i = 0; i < 34; i++)
		s_originalVtableFuncs[i] = nativeVtable[i];

	s_originalGetTextSize = reinterpret_cast<GetTextSize_t>(nativeVtable[4]);

	s_originalDrawFuncs[0] = s_originalVtableFuncs[28];
	s_originalDrawFuncs[1] = s_originalVtableFuncs[29];
	s_originalDrawFuncs[2] = s_originalVtableFuncs[30];
	s_originalDrawFuncs[3] = s_originalVtableFuncs[31];
	s_originalDrawFuncs[4] = s_originalVtableFuncs[32];
	s_originalDrawFuncs[5] = s_originalVtableFuncs[33];

	// Utility functions [0-45]
	s_ruiApiExtended[0]  = reinterpret_cast<void*>(&VtableWrapper_0);     // SetHidden
	s_ruiApiExtended[1]  = reinterpret_cast<void*>(&VtableWrapper_1);     // SetNoRender
	s_ruiApiExtended[2]  = reinterpret_cast<void*>(&Rui_CodeAssert);      // CodeAssert
	s_ruiApiExtended[3]  = reinterpret_cast<void*>(&Rui_GetWidgetSizeArray);
	s_ruiApiExtended[4]  = reinterpret_cast<void*>(&Rui_GetTextSizeCompat);
	s_ruiApiExtended[5]  = reinterpret_cast<void*>(&VtableWrapper_5);     // unk1
	s_ruiApiExtended[6]  = reinterpret_cast<void*>(&VtableWrapper_6);     // ExecuteTransform
	s_ruiApiExtended[7]  = reinterpret_cast<void*>(&VtableWrapper_7);     // ExecuteTransformAndResize
	s_ruiApiExtended[8]  = reinterpret_cast<void*>(&VtableWrapper_8);     // SNPrintF
	s_ruiApiExtended[9]  = reinterpret_cast<void*>(&VtableWrapper_9);     // Localize
	s_ruiApiExtended[10] = reinterpret_cast<void*>(&VtableWrapper_10);    // ToUpper
	s_ruiApiExtended[11] = reinterpret_cast<void*>(&VtableWrapper_11);    // FormatNumber
	s_ruiApiExtended[12] = reinterpret_cast<void*>(&Rui_LocalizeNumber);
	s_ruiApiExtended[13] = reinterpret_cast<void*>(&Rui_FormatAndLocalizeNumber);
	s_ruiApiExtended[14] = reinterpret_cast<void*>(&Rui_ReturnFalse);
	s_ruiApiExtended[15] = reinterpret_cast<void*>(&VtableWrapper_12);    // SrgbToLinear
	s_ruiApiExtended[16] = reinterpret_cast<void*>(&VtableWrapper_13);    // SinNorm
	s_ruiApiExtended[17] = reinterpret_cast<void*>(&Rui_Atan2Norm);
	s_ruiApiExtended[18] = reinterpret_cast<void*>(&VtableWrapper_14);    // RandomFloat
	s_ruiApiExtended[19] = reinterpret_cast<void*>(&VtableWrapper_15);    // ProjectWorldPoint
	s_ruiApiExtended[20] = reinterpret_cast<void*>(&VtableWrapper_16);    // LetterboxSize
	s_ruiApiExtended[21] = reinterpret_cast<void*>(&VtableWrapper_17);    // NestedUiSize
	s_ruiApiExtended[22] = reinterpret_cast<void*>(&Rui_AssetLookupCompat);
	s_ruiApiExtended[23] = reinterpret_cast<void*>(&VtableWrapper_19);    // ImageToIconString
	s_ruiApiExtended[24] = reinterpret_cast<void*>(&VtableWrapper_20);    // Animate1D
	s_ruiApiExtended[25] = reinterpret_cast<void*>(&VtableWrapper_21);    // Animate2D
	s_ruiApiExtended[26] = reinterpret_cast<void*>(&VtableWrapper_22);    // Animate3D
	s_ruiApiExtended[27] = reinterpret_cast<void*>(&VtableWrapper_23);    // Animate4D
	s_ruiApiExtended[28] = reinterpret_cast<void*>(&VtableWrapper_24);    // NestedUiFinalSize
	s_ruiApiExtended[29] = reinterpret_cast<void*>(&VtableWrapper_25);    // StringToHash
	s_ruiApiExtended[30] = reinterpret_cast<void*>(&VtableWrapper_26);    // GetKeyColor
	s_ruiApiExtended[31] = reinterpret_cast<void*>(&VtableWrapper_27);    // GetViewportScale
	s_ruiApiExtended[32] = reinterpret_cast<void*>(&Rui_NestedUiAutoSize);
	s_ruiApiExtended[33] = reinterpret_cast<void*>(&Rui_FlexSpaceCalc);
	s_ruiApiExtended[34] = reinterpret_cast<void*>(&Rui_WidgetSizeRatio);
	s_ruiApiExtended[35] = reinterpret_cast<void*>(&Rui_MeasureTextByFontHash);
	s_ruiApiExtended[36] = reinterpret_cast<void*>(&Rui_ProjectionHelper);
	s_ruiApiExtended[37] = reinterpret_cast<void*>(&Rui_AlignmentLookup);
	s_ruiApiExtended[38] = reinterpret_cast<void*>(&Rui_GuardCheckIcallNop);
	s_ruiApiExtended[39] = reinterpret_cast<void*>(&Rui_GetLocString_36);
	s_ruiApiExtended[40] = reinterpret_cast<void*>(&Rui_GetLocString_37);
	s_ruiApiExtended[41] = reinterpret_cast<void*>(&Rui_GetLocString_38);
	s_ruiApiExtended[42] = reinterpret_cast<void*>(&Rui_GetLocString_39);
	s_ruiApiExtended[43] = reinterpret_cast<void*>(&Rui_GetLocString_40);
	s_ruiApiExtended[44] = reinterpret_cast<void*>(&Rui_GetLocString_41);
	s_ruiApiExtended[45] = reinterpret_cast<void*>(&Rui_GetLocString_42);

	// Draw functions [46-51]
	s_ruiApiExtended[46] = reinterpret_cast<void*>(&Rui_DrawWrapper_RenderText);
	s_ruiApiExtended[47] = reinterpret_cast<void*>(&Rui_DrawWrapper_DrawImage);
	s_ruiApiExtended[48] = reinterpret_cast<void*>(&Rui_DrawWrapper_DrawFill);
	s_ruiApiExtended[49] = reinterpret_cast<void*>(&Rui_DrawWrapper_DrawLine);
	s_ruiApiExtended[50] = reinterpret_cast<void*>(&Rui_DrawWrapper_DrawFont);
	s_ruiApiExtended[51] = reinterpret_cast<void*>(&Rui_DrawWrapper_DrawClip);

	s_extendedVtableInitialized = true;
	DevMsg(eDLL_T::RTECH, "RUI: Extended vtable initialized (%d entries: 46 utility + 6 draw)\n", RUI_MAX_VTABLE_ENTRIES);
}

//=============================================================================
// VTABLE REFERENCE PATCHING
//=============================================================================

static bool s_vtableRefPatched = false;
static void* s_allocatedVtable = nullptr;

constexpr uintptr_t LEA_VTABLE_RVA_1 = 0x2FC7E2;
constexpr uintptr_t LEA_VTABLE_RVA_2 = 0x2FD6C5;
constexpr uintptr_t DRAW_DISPATCH_CALL_RVA = 0x2FD0BE;
constexpr uintptr_t DRAW_FUNCTION_TABLE_RVA = 0x1300BB0;

static bool Rui_PatchVtableLEA(uintptr_t leaAddr, uintptr_t newVtableAddr, const char* name)
{
	uint8_t* bytes = reinterpret_cast<uint8_t*>(leaAddr);
	if (bytes[0] != 0x48 || bytes[1] != 0x8D || bytes[2] != 0x0D)
	{
		Warning(eDLL_T::RTECH, "RUI: LEA patch failed at %s - unexpected opcode\n", name);
		return false;
	}

	int32_t* pOffset = reinterpret_cast<int32_t*>(leaAddr + 3);
	uintptr_t nextInstr = leaAddr + 7;

	int64_t newOffsetFull = static_cast<int64_t>(newVtableAddr) - static_cast<int64_t>(nextInstr);
	if (newOffsetFull > INT32_MAX || newOffsetFull < INT32_MIN)
		return false;

	DWORD oldProtect;
	if (!VirtualProtect(pOffset, 4, PAGE_EXECUTE_READWRITE, &oldProtect))
		return false;

	*pOffset = static_cast<int32_t>(newOffsetFull);
	VirtualProtect(pOffset, 4, oldProtect, &oldProtect);

	return true;
}

static bool Rui_PatchDrawDispatch(uintptr_t moduleBase, uintptr_t newVtableAddr, int drawStartIndex)
{
	uintptr_t callAddr = moduleBase + DRAW_DISPATCH_CALL_RVA;
	uint8_t* bytes = reinterpret_cast<uint8_t*>(callAddr);

	if (bytes[0] != 0x41 || bytes[1] != 0xff || bytes[2] != 0x94 || bytes[3] != 0xc6)
		return false;

	int32_t* pDisp = reinterpret_cast<int32_t*>(callAddr + 4);
	uintptr_t vtableOffset = static_cast<uintptr_t>(drawStartIndex) * sizeof(void*);

	int64_t newDispFull = static_cast<int64_t>(newVtableAddr + vtableOffset) - static_cast<int64_t>(moduleBase);
	if (newDispFull > INT32_MAX || newDispFull < INT32_MIN)
		return false;

	DWORD oldProtect;
	if (!VirtualProtect(pDisp, 4, PAGE_EXECUTE_READWRITE, &oldProtect))
		return false;

	*pDisp = static_cast<int32_t>(newDispFull);
	VirtualProtect(pDisp, 4, oldProtect, &oldProtect);

	return true;
}

static bool Rui_PatchDrawFunctionTable(uintptr_t moduleBase)
{
	uintptr_t drawTableAddr = moduleBase + DRAW_FUNCTION_TABLE_RVA;
	void** drawTable = reinterpret_cast<void**>(drawTableAddr);

	DWORD oldProtect;
	if (!VirtualProtect(drawTable, 6 * sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
	{
		Warning(eDLL_T::RTECH, "RUI: Failed to unprotect draw function table\n");
		return false;
	}

	drawTable[0] = reinterpret_cast<void*>(&Rui_DrawWrapper_RenderText);
	drawTable[1] = reinterpret_cast<void*>(&Rui_DrawWrapper_DrawImage);
	drawTable[2] = reinterpret_cast<void*>(&Rui_DrawWrapper_DrawFill);
	drawTable[3] = reinterpret_cast<void*>(&Rui_DrawWrapper_DrawLine);
	drawTable[4] = reinterpret_cast<void*>(&Rui_DrawWrapper_DrawFont);
	drawTable[5] = reinterpret_cast<void*>(&Rui_DrawWrapper_DrawClip);

	VirtualProtect(drawTable, 6 * sizeof(void*), oldProtect, &oldProtect);

	DevMsg(eDLL_T::RTECH, "RUI: Draw function table patched (6 entries)\n");
	return true;
}

static void Rui_PatchVtableReferences()
{
	if (s_vtableRefPatched)
		return;

	uintptr_t moduleBase = g_GameDll.GetModuleBase();
	if (!moduleBase)
		return;

	uintptr_t originalVtable = moduleBase + 0x1300AD0;
	size_t vtableSize = RUI_MAX_VTABLE_ENTRIES * sizeof(void*);

	s_allocatedVtable = AllocateNearAddress(originalVtable, vtableSize);
	if (!s_allocatedVtable)
	{
		Warning(eDLL_T::RTECH, "RUI: Failed to allocate vtable\n");
		return;
	}

	memcpy(s_allocatedVtable, s_ruiApiExtended, vtableSize);
	uintptr_t newVtable = reinterpret_cast<uintptr_t>(s_allocatedVtable);

	constexpr int drawStartIndex = 46;

	bool success1 = Rui_PatchVtableLEA(moduleBase + LEA_VTABLE_RVA_1, newVtable, "LEA1");
	bool success2 = Rui_PatchVtableLEA(moduleBase + LEA_VTABLE_RVA_2, newVtable, "LEA2");
	bool success3 = Rui_PatchDrawDispatch(moduleBase, newVtable, drawStartIndex);

	if (success1 && success2 && success3)
	{
		s_vtableRefPatched = true;
		DevMsg(eDLL_T::RTECH, "RUI: Vtable references patched (%d entries)\n", RUI_MAX_VTABLE_ENTRIES);
	}
}

//=============================================================================
// DETOUR SETUP
//=============================================================================

void V_Rui::Detour(const bool bAttach) const
{
	DetourSetup(&v_Rui_Draw, &Rui_Draw_Wrapper, bAttach);

	if (!s_ruiApi)
	{
		Error(eDLL_T::RTECH, NO_ERROR, "s_ruiApi is null\n");
		return;
	}

	if (bAttach)
	{
		// Crash protection patches. TODO(cafe): test stability without them some may not be needed.
		Rui_PatchSetArgNullCheck();
		Rui_PatchUpdateWidgetNullCheck();
		Rui_PatchPostCallbackNullCheck();
		Rui_PatchVtableFunc19NullCheck();
		Rui_PatchVtableFunc20NullCheck();
		Rui_PatchDestroyWidgetCleanup();
		Rui_PatchProcessWidgetsNullInstance();
		Rui_PatchProcessWidgetsDefCheck();
		Rui_PatchDrawImageTilingLite();

		// Compatibility layer
		DevMsg(eDLL_T::RTECH, "RUI: Initializing compatibility layer\n");
		Rui_PatchRuiFuncStateOffset();
		Rui_InitExtendedVtable();
		Rui_PatchVtableReferences();

		uintptr_t moduleBase = g_GameDll.GetModuleBase();
		if (moduleBase)
			Rui_PatchDrawFunctionTable(moduleBase);

		s_ruiApi = reinterpret_cast<RuiFuncs_s*>(s_ruiApiExtended);
		DevMsg(eDLL_T::RTECH, "RUI: Compatibility layer active (%d entries)\n", RUI_MAX_VTABLE_ENTRIES);
	}
}
