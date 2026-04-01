//=============================================================================//
//
// Purpose: Color palette system with native C++ API, ConVar color bindings,
//          number formatting, and slider control.
//
//=============================================================================//

#include "core/stdafx.h"
#include "tier1/convar.h"
#include "tier1/cvar.h"
#include "mathlib/mathlib.h"

#include "vscript/vscript.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "vscript/languages/squirrel_re/include/squirrel.h"

#include "game/shared/vscript_gamedll_defs.h"
#include "game/shared/vscript_shared.h"

#include "vscript_client.h"
#include "vscript_colorpalette.h"

#include <unordered_map>
#include <string>

//=============================================================================
// Forward declarations
//=============================================================================
static void ReticleColorChangeCB(IConVar* var, const char* pOldValue,
	float flOldValue, ChangeUserData_t pUserData);

//=============================================================================
// ConVars
//=============================================================================
static ConVar colorPalette_laserSight_minBrightness("colorPalette_laserSight_minBrightness",
	"0.5", FCVAR_RELEASE, "Minimum HSV value for laser sight color.", "0 1");
static ConVar colorPalette_laserSight_maxBrightness("colorPalette_laserSight_maxBrightness",
	"1.0", FCVAR_RELEASE, "Maximum HSV value for laser sight color.", "0 1");

static ConVar reticle_color("reticle_color", "", FCVAR_RELEASE,
	"Reticle color override as 'R G B' (0-255). Empty to reset.",
	&ReticleColorChangeCB);

static ConVar lobby_theme_color("lobby_theme_color", "199 21 11", FCVAR_RELEASE | FCVAR_ARCHIVE,
	"Lobby theme color as 'R G B' (0-255). Default: 199 21 11.");

//=============================================================================
// Color palette storage
//=============================================================================
static constexpr int MAX_PALETTE_COLORS = 256;

struct ColorPalette_t
{
	SQVector3D defaultColors[MAX_PALETTE_COLORS];
	SQVector3D overrideColors[MAX_PALETTE_COLORS];
	bool isOverridden[MAX_PALETTE_COLORS];
	std::unordered_map<std::string, int> nameToIndex;
	int nextAutoIndex;
	bool initialized;

	ColorPalette_t()
		: nextAutoIndex(0)
		, initialized(true)
	{
		for (int i = 0; i < MAX_PALETTE_COLORS; i++)
		{
			defaultColors[i].Init(255.0f, 255.0f, 255.0f);
			overrideColors[i].Init(255.0f, 255.0f, 255.0f);
			isOverridden[i] = false;
		}
	}
};

static ColorPalette_t s_colorPalette;

//=============================================================================
// Slider control value storage
//=============================================================================
static std::unordered_map<uintptr_t, float> s_sliderValues;

//=============================================================================
// Native C++ ColorPalette API
//=============================================================================

//-----------------------------------------------------------------------------
// Purpose: returns true once the color palette is ready for use
//-----------------------------------------------------------------------------
bool ColorPalette_HasBeenInitialized()
{
	return s_colorPalette.initialized;
}

//-----------------------------------------------------------------------------
// Purpose: looks up a color name and returns its palette index, or -1
//-----------------------------------------------------------------------------
int ColorPalette_GetIDFromString(const char* colorName)
{
	auto it = s_colorPalette.nameToIndex.find(colorName);
	if (it != s_colorPalette.nameToIndex.end())
		return it->second;
	return -1;
}

//-----------------------------------------------------------------------------
// Purpose: gets or assigns a palette index for a color name
//          Returns -1 if the palette is full.
//-----------------------------------------------------------------------------
static int ColorPalette_GetOrAssignID(const char* colorName)
{
	auto it = s_colorPalette.nameToIndex.find(colorName);
	if (it != s_colorPalette.nameToIndex.end())
		return it->second;

	if (s_colorPalette.nextAutoIndex >= MAX_PALETTE_COLORS)
		return -1;

	const int idx = s_colorPalette.nextAutoIndex++;
	s_colorPalette.nameToIndex[colorName] = idx;
	return idx;
}

//-----------------------------------------------------------------------------
// Purpose: gets color from palette by ID (override if set, else default)
//          Returns white (255,255,255) for out-of-range IDs.
//-----------------------------------------------------------------------------
Vector3D ColorPalette_GetColorFromID(int colorID)
{
	if (colorID < 0 || colorID >= MAX_PALETTE_COLORS)
		return Vector3D(255.0f, 255.0f, 255.0f);

	if (s_colorPalette.isOverridden[colorID])
	{
		const SQVector3D& c = s_colorPalette.overrideColors[colorID];
		return Vector3D(c.x, c.y, c.z);
	}

	const SQVector3D& c = s_colorPalette.defaultColors[colorID];
	return Vector3D(c.x, c.y, c.z);
}

//-----------------------------------------------------------------------------
// Purpose: gets the default (non-overridden) color from palette by ID
//          Always reads from the base table, ignoring overrides.
//-----------------------------------------------------------------------------
Vector3D ColorPalette_GetDefaultColorFromID(int colorID)
{
	if (colorID < 0 || colorID >= MAX_PALETTE_COLORS)
		return Vector3D(255.0f, 255.0f, 255.0f);

	const SQVector3D& c = s_colorPalette.defaultColors[colorID];
	return Vector3D(c.x, c.y, c.z);
}

//-----------------------------------------------------------------------------
// Purpose: returns true if the color at the given ID has been overridden
//-----------------------------------------------------------------------------
bool ColorPalette_IsColorCustomized(int colorID)
{
	if (colorID < 0 || colorID >= MAX_PALETTE_COLORS)
		return false;
	return s_colorPalette.isOverridden[colorID];
}

//-----------------------------------------------------------------------------
// Purpose: overrides a named color in the palette
//          Finds or assigns an index, stores the override color.
//-----------------------------------------------------------------------------
void ColorPalette_OverrideColorByName(const char* colorName, const Vector3D& color)
{
	const int idx = ColorPalette_GetOrAssignID(colorName);
	if (idx < 0)
		return;

	s_colorPalette.overrideColors[idx].Init(color.x, color.y, color.z);
	s_colorPalette.isOverridden[idx] = true;
}

//-----------------------------------------------------------------------------
// Purpose: clears the override for a named color, reverting to default
//-----------------------------------------------------------------------------
void ColorPalette_ResetColor(const char* colorName)
{
	auto it = s_colorPalette.nameToIndex.find(colorName);
	if (it == s_colorPalette.nameToIndex.end())
		return;

	s_colorPalette.isOverridden[it->second] = false;
}

//-----------------------------------------------------------------------------
// Purpose: returns the number of registered colors in the palette
//-----------------------------------------------------------------------------
int ColorPalette_GetColorCount()
{
	return s_colorPalette.nextAutoIndex;
}

//-----------------------------------------------------------------------------
// Purpose: clamps an RGB color (0-255) to minimum 50% HSV brightness
//          Native C++ version callable from ConVar callbacks and other code.
//-----------------------------------------------------------------------------
void ColorPalette_ClampAndValidateColor(Vector3D& result, const Vector3D& color)
{
	const Vector3D rgb(color.x / 255.0f, color.y / 255.0f, color.z / 255.0f);
	Vector3D hsv;
	RGBtoHSV(rgb, hsv);

	if (hsv.z < 0.5f)
		hsv.z = 0.5f;

	Vector3D rgbOut;
	HSVtoRGB(hsv, rgbOut);

	result.x = (float)Clamp((int)(rgbOut.x * 255.0f), 0, 255);
	result.y = (float)Clamp((int)(rgbOut.y * 255.0f), 0, 255);
	result.z = (float)Clamp((int)(rgbOut.z * 255.0f), 0, 255);
}

//=============================================================================
// reticle_color ConVar change callback
// Parses "R G B" string, clamps brightness, applies override on "RETICLE".
//=============================================================================
static void ReticleColorChangeCB(IConVar* var, const char* pOldValue,
	float flOldValue, ChangeUserData_t pUserData)
{
	if (!ColorPalette_HasBeenInitialized())
		return;

	ConVar* pCVar = g_pCVar->FindVar(var->GetName());
	if (!pCVar)
		return;

	const char* str = pCVar->GetString();

	// Empty string = reset to default color
	if (!str || !*str)
	{
		ColorPalette_ResetColor("RETICLE");
		return;
	}

	// Parse "R G B" space-separated
	char* end;
	float r = (float)strtod(str, &end);
	float g = (float)strtod(end, &end);
	float b = (float)strtod(end, &end);

	// Default to 100,100,100 if all components are zero
	if (r == 0.0f && g == 0.0f && b == 0.0f)
	{
		r = 100.0f;
		g = 100.0f;
		b = 100.0f;
	}

	Vector3D color(r, g, b);
	Vector3D clamped;
	ColorPalette_ClampAndValidateColor(clamped, color);
	ColorPalette_OverrideColorByName("RETICLE", clamped);
}

//=============================================================================
// Script function implementations
//=============================================================================

//-----------------------------------------------------------------------------
// Group A: Pure math color conversion
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: converts RGB (0-255) to HSV (H=0-360, S=0-1, V=0-1)
//-----------------------------------------------------------------------------
static SQRESULT Script_ColorPalette_RGBtoHSV_Impl(HSQUIRRELVM v)
{
	const SQVector3D* rgbVec = nullptr;
	sq_getvector(v, 2, &rgbVec);

	const Vector3D rgb(rgbVec->x / 255.0f, rgbVec->y / 255.0f, rgbVec->z / 255.0f);
	Vector3D hsv;
	RGBtoHSV(rgb, hsv);

	const SQVector3D result(hsv.x, hsv.y, hsv.z);
	sq_pushvector(v, &result);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_ColorPalette_RGBtoHSV(HSQUIRRELVM v) { return Script_ColorPalette_RGBtoHSV_Impl(v); }
static SQRESULT UIScript_ColorPalette_RGBtoHSV(HSQUIRRELVM v) { return Script_ColorPalette_RGBtoHSV_Impl(v); }

//-----------------------------------------------------------------------------
// Purpose: converts HSV (H=0-360, S=0-1, V=0-1) to RGB (0-255)
//-----------------------------------------------------------------------------
static SQRESULT Script_ColorPalette_HSVtoRGB_Impl(HSQUIRRELVM v)
{
	const SQVector3D* hsvVec = nullptr;
	sq_getvector(v, 2, &hsvVec);

	const Vector3D hsv(hsvVec->x, hsvVec->y, hsvVec->z);
	Vector3D rgb;
	HSVtoRGB(hsv, rgb);

	const SQVector3D result(
		(SQFloat)(int)(rgb.x * 255.0f),
		(SQFloat)(int)(rgb.y * 255.0f),
		(SQFloat)(int)(rgb.z * 255.0f));
	sq_pushvector(v, &result);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_ColorPalette_HSVtoRGB(HSQUIRRELVM v) { return Script_ColorPalette_HSVtoRGB_Impl(v); }
static SQRESULT UIScript_ColorPalette_HSVtoRGB(HSQUIRRELVM v) { return Script_ColorPalette_HSVtoRGB_Impl(v); }

//-----------------------------------------------------------------------------
// Purpose: script wrapper for ColorPalette_ClampAndValidateColor
//-----------------------------------------------------------------------------
static SQRESULT Script_ColorPalette_ClampAndValidateColor_Impl(HSQUIRRELVM v)
{
	const SQVector3D* rgbVec = nullptr;
	sq_getvector(v, 2, &rgbVec);

	Vector3D color(rgbVec->x, rgbVec->y, rgbVec->z);
	Vector3D clamped;
	ColorPalette_ClampAndValidateColor(clamped, color);

	const SQVector3D result(clamped.x, clamped.y, clamped.z);
	sq_pushvector(v, &result);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_ColorPalette_ClampAndValidateColor(HSQUIRRELVM v) { return Script_ColorPalette_ClampAndValidateColor_Impl(v); }
static SQRESULT UIScript_ColorPalette_ClampAndValidateColor(HSQUIRRELVM v) { return Script_ColorPalette_ClampAndValidateColor_Impl(v); }

//-----------------------------------------------------------------------------
// Group B: ConVar-based color functions
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: clamps color brightness using ConVar min/max for laser sights
//-----------------------------------------------------------------------------
static SQRESULT Script_ColorPalette_ClampAndValidateLaserSightColor_Impl(HSQUIRRELVM v)
{
	const SQVector3D* rgbVec = nullptr;
	sq_getvector(v, 2, &rgbVec);

	const float minBrightness = colorPalette_laserSight_minBrightness.GetFloat();
	const float maxBrightness = colorPalette_laserSight_maxBrightness.GetFloat();

	const Vector3D rgb(rgbVec->x / 255.0f, rgbVec->y / 255.0f, rgbVec->z / 255.0f);
	Vector3D hsv;
	RGBtoHSV(rgb, hsv);

	hsv.z = Clamp(hsv.z, minBrightness, maxBrightness);

	Vector3D rgbOut;
	HSVtoRGB(hsv, rgbOut);

	const SQVector3D result(
		(SQFloat)Clamp((int)(rgbOut.x * 255.0f), 0, 255),
		(SQFloat)Clamp((int)(rgbOut.y * 255.0f), 0, 255),
		(SQFloat)Clamp((int)(rgbOut.z * 255.0f), 0, 255));
	sq_pushvector(v, &result);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_ColorPalette_ClampAndValidateLaserSightColor(HSQUIRRELVM v) { return Script_ColorPalette_ClampAndValidateLaserSightColor_Impl(v); }
static SQRESULT UIScript_ColorPalette_ClampAndValidateLaserSightColor(HSQUIRRELVM v) { return Script_ColorPalette_ClampAndValidateLaserSightColor_Impl(v); }

//-----------------------------------------------------------------------------
// Purpose: reads a ConVar as a color vector (0-255 RGB)
//-----------------------------------------------------------------------------
static SQRESULT Script_GetConVarColor_Impl(HSQUIRRELVM v)
{
	const SQChar* cvarName = nullptr;
	sq_getstring(v, 2, &cvarName);

	ConVar* pVar = g_pCVar->FindVar(cvarName);
	if (!pVar)
	{
		v_SQVM_ScriptError("ConVar '%s' not found", cvarName);
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	if (pVar->IsFlagSet(FCVAR_PROTECTED))
	{
		v_SQVM_ScriptError("ConVar '%s' is protected", cvarName);
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	if (pVar->IsFlagSet(FCVAR_NEVER_AS_STRING))
	{
		v_SQVM_ScriptError("ConVar '%s' is FCVAR_NEVER_AS_STRING", cvarName);
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	const Color c = pVar->GetColor();
	const SQVector3D result((SQFloat)c.r(), (SQFloat)c.g(), (SQFloat)c.b());
	sq_pushvector(v, &result);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_GetConVarColor(HSQUIRRELVM v) { return Script_GetConVarColor_Impl(v); }
static SQRESULT UIScript_GetConVarColor(HSQUIRRELVM v) { return Script_GetConVarColor_Impl(v); }

//-----------------------------------------------------------------------------
// Purpose: sets a ConVar from a color vector (0-255 RGB)
//-----------------------------------------------------------------------------
static SQRESULT Script_SetConVarColor_Impl(HSQUIRRELVM v)
{
	const SQChar* cvarName = nullptr;
	sq_getstring(v, 2, &cvarName);

	const SQVector3D* colorVec = nullptr;
	sq_getvector(v, 3, &colorVec);

	ConVar* pVar = g_pCVar->FindVar(cvarName);
	if (!pVar)
	{
		v_SQVM_ScriptError("ConVar '%s' not found", cvarName);
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	if (pVar->IsFlagSet(FCVAR_PROTECTED))
	{
		v_SQVM_ScriptError("ConVar '%s' is protected", cvarName);
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	if (pVar->IsFlagSet(FCVAR_NEVER_AS_STRING))
	{
		v_SQVM_ScriptError("ConVar '%s' is FCVAR_NEVER_AS_STRING", cvarName);
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	const Color c(
		Clamp((int)colorVec->x, 0, 255),
		Clamp((int)colorVec->y, 0, 255),
		Clamp((int)colorVec->z, 0, 255),
		255);
	pVar->SetValue(c);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_SetConVarColor(HSQUIRRELVM v) { return Script_SetConVarColor_Impl(v); }
static SQRESULT UIScript_SetConVarColor(HSQUIRRELVM v) { return Script_SetConVarColor_Impl(v); }

//-----------------------------------------------------------------------------
// Group C: Color palette script functions
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: script wrapper - gets default (non-overridden) color from palette
//-----------------------------------------------------------------------------
static SQRESULT Script_ColorPalette_GetDefaultColorFromID_Impl(HSQUIRRELVM v)
{
	SQInteger colorID = 0;
	sq_getinteger(v, 2, &colorID);

	const Vector3D color = ColorPalette_GetDefaultColorFromID((int)colorID);
	const SQVector3D result(color.x, color.y, color.z);
	sq_pushvector(v, &result);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_ColorPalette_GetDefaultColorFromID(HSQUIRRELVM v) { return Script_ColorPalette_GetDefaultColorFromID_Impl(v); }
static SQRESULT UIScript_ColorPalette_GetDefaultColorFromID(HSQUIRRELVM v) { return Script_ColorPalette_GetDefaultColorFromID_Impl(v); }

//-----------------------------------------------------------------------------
// Purpose: script wrapper - gets color from palette (override if set)
//-----------------------------------------------------------------------------
static SQRESULT Script_ColorPalette_GetColorFromID_Impl(HSQUIRRELVM v)
{
	SQInteger colorID = 0;
	sq_getinteger(v, 2, &colorID);

	const Vector3D color = ColorPalette_GetColorFromID((int)colorID);
	const SQVector3D result(color.x, color.y, color.z);
	sq_pushvector(v, &result);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_ColorPalette_GetColorFromID(HSQUIRRELVM v) { return Script_ColorPalette_GetColorFromID_Impl(v); }
static SQRESULT UIScript_ColorPalette_GetColorFromID(HSQUIRRELVM v) { return Script_ColorPalette_GetColorFromID_Impl(v); }

//-----------------------------------------------------------------------------
// Purpose: script wrapper - gets palette index from color name
//-----------------------------------------------------------------------------
static SQRESULT Script_ColorPalette_GetIDFromString_Impl(HSQUIRRELVM v)
{
	const SQChar* colorName = nullptr;
	sq_getstring(v, 2, &colorName);

	sq_pushinteger(v, ColorPalette_GetIDFromString(colorName));
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_ColorPalette_GetIDFromString(HSQUIRRELVM v) { return Script_ColorPalette_GetIDFromString_Impl(v); }
static SQRESULT UIScript_ColorPalette_GetIDFromString(HSQUIRRELVM v) { return Script_ColorPalette_GetIDFromString_Impl(v); }

//-----------------------------------------------------------------------------
// Purpose: script wrapper - checks if a palette color has been overridden
//-----------------------------------------------------------------------------
static SQRESULT Script_ColorPalette_IsColorCustomized_Impl(HSQUIRRELVM v)
{
	SQInteger colorID = 0;
	sq_getinteger(v, 2, &colorID);

	sq_pushbool(v, ColorPalette_IsColorCustomized((int)colorID));
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_ColorPalette_IsColorCustomized(HSQUIRRELVM v) { return Script_ColorPalette_IsColorCustomized_Impl(v); }
static SQRESULT UIScript_ColorPalette_IsColorCustomized(HSQUIRRELVM v) { return Script_ColorPalette_IsColorCustomized_Impl(v); }

//-----------------------------------------------------------------------------
// Purpose: stores a named color override in the color palette (UI context)
//-----------------------------------------------------------------------------
static SQRESULT UIScript_OverrideColor(HSQUIRRELVM v)
{
	const SQVector3D* colorVec = nullptr;
	sq_getvector(v, 2, &colorVec);

	const SQChar* colorName = nullptr;
	sq_getstring(v, 3, &colorName);

	const Vector3D color(colorVec->x, colorVec->y, colorVec->z);
	ColorPalette_OverrideColorByName(colorName, color);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Group D: HUD slider control
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: sets slider control value for a HUD element
//          Since SliderControl doesn't exist in r5sdk, we provide
//          map-based storage keyed by entity handle.
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_SliderControl_SetCurrentValue_Impl(HSQUIRRELVM v)
{
	HSQOBJECT entityObj;
	sq_getstackobj(v, 2, &entityObj);

	SQFloat value = 0.0f;
	sq_getfloat(v, 3, &value);

	const uintptr_t key = (uintptr_t)entityObj._unVal.pUserPointer;
	s_sliderValues[key] = static_cast<float>(value);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_SliderControl_SetCurrentValue(HSQUIRRELVM v) { return Script_Hud_SliderControl_SetCurrentValue_Impl(v); }
static SQRESULT UIScript_Hud_SliderControl_SetCurrentValue(HSQUIRRELVM v) { return Script_Hud_SliderControl_SetCurrentValue_Impl(v); }

//-----------------------------------------------------------------------------
// Purpose: gets slider control value from a HUD element
//          Returns the last value set via SetCurrentValue, or 0.0f if none.
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_SliderControl_GetCurrentValue_Impl(HSQUIRRELVM v)
{
	HSQOBJECT entityObj;
	sq_getstackobj(v, 2, &entityObj);

	const uintptr_t key = (uintptr_t)entityObj._unVal.pUserPointer;
	auto it = s_sliderValues.find(key);

	sq_pushfloat(v, it != s_sliderValues.end() ? it->second : 0.0f);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_SliderControl_GetCurrentValue(HSQUIRRELVM v) { return Script_Hud_SliderControl_GetCurrentValue_Impl(v); }
static SQRESULT UIScript_Hud_SliderControl_GetCurrentValue(HSQUIRRELVM v) { return Script_Hud_SliderControl_GetCurrentValue_Impl(v); }

//-----------------------------------------------------------------------------
// Group E: Number formatting
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: converts RUI-style format string to printf format
//          RUI format uses example-based templates:
//            '1'   = number placeholder
//            '0'   = zero padding before '1'
//            '_'   = space padding before '1'
//            '+'   = force sign display
//            '.'   = decimal separator
//            '00'  = fixed decimal places after '.'
//            '99'  = trimmed trailing zeros after '.'
//          Examples: "001.00" -> "%06.2f", "+1" -> "%+d", "__1.99" -> "%5.2f"
//-----------------------------------------------------------------------------
static void ConvertRuiStyleFormatToPrintfFormat(const char* ruiFormat, char* out,
	int outSize, bool isInteger, bool* trimTrailingZeros)
{
	*trimTrailingZeros = false;

	if (!ruiFormat || !*ruiFormat)
	{
		snprintf(out, outSize, isInteger ? "%%d" : "%%f");
		return;
	}

	const char* p = ruiFormat;

	// Phase 1: Parse prefix flags
	bool forceSign = false;
	int spacePad = 0;

	if (*p == '+') { forceSign = true; p++; }
	while (*p == '_') { spacePad++; p++; }

	// Count leading zeros
	int zeroPad = 0;
	while (*p == '0') { zeroPad++; p++; }

	// Skip the example digit '1'
	if (*p == '1') p++;

	const int intWidth = spacePad + zeroPad + 1;

	// Phase 2: Parse decimal part
	int fracDigits = 0;
	bool hasFrac = false;

	if (*p == '.')
	{
		hasFrac = true;
		p++;
		const char* fracStart = p;
		while (*p == '0' || *p == '9') { fracDigits++; p++; }

		// All '9's after dot means trim trailing zeros
		if (fracDigits > 0)
		{
			bool allNines = true;
			for (int i = 0; i < fracDigits; i++)
			{
				if (fracStart[i] != '9') { allNines = false; break; }
			}
			*trimTrailingZeros = allNines;
		}
	}

	// Phase 3: Build printf format string
	char flags[4] = {0};
	int fi = 0;
	if (forceSign) flags[fi++] = '+';
	if (zeroPad > 0) flags[fi++] = '0';

	if (hasFrac || !isInteger)
	{
		const int totalWidth = (intWidth > 1 || zeroPad > 0)
			? intWidth + 1 + fracDigits : 0;

		if (totalWidth > 0)
			snprintf(out, outSize, "%%%s%d.%df", flags, totalWidth, fracDigits);
		else
			snprintf(out, outSize, "%%%s.%df", flags, fracDigits);
	}
	else
	{
		if (intWidth > 1)
			snprintf(out, outSize, "%%%s%dd", flags, intWidth);
		else
			snprintf(out, outSize, "%%%sd", flags);
	}
}

//-----------------------------------------------------------------------------
// Purpose: strips trailing zeros after the decimal point
//          "3.500" -> "3.5", "3.000" -> "3", "3.0" -> "3"
//-----------------------------------------------------------------------------
static void TrimTrailingZeros(char* buffer)
{
	char* dot = strchr(buffer, '.');
	if (!dot) return;

	char* end = buffer + strlen(buffer) - 1;
	while (end > dot && *end == '0') end--;

	if (end == dot)
		*end = '\0';
	else
		*(end + 1) = '\0';
}

//-----------------------------------------------------------------------------
// Purpose: inserts thousands separators into a number string
//-----------------------------------------------------------------------------
static void InsertThousandsSeparators(char* buffer, int bufferSize)
{
	char* dot = strchr(buffer, '.');
	char* intEnd = dot ? dot : buffer + strlen(buffer);

	// Account for sign character
	int startPos = 0;
	if (buffer[0] == '-' || buffer[0] == '+' || buffer[0] == ' ')
		startPos = 1;

	const int intLen = (int)(intEnd - buffer);
	const int digitCount = intLen - startPos;

	if (digitCount <= 3)
		return;

	const int numSeps = (digitCount - 1) / 3;
	const int newLen = (int)strlen(buffer) + numSeps;

	if (newLen >= bufferSize - 1)
		return;

	const int tailLen = (int)strlen(intEnd);
	memmove(intEnd + numSeps, intEnd, tailLen + 1);

	int src = intLen - 1;
	int dst = intLen + numSeps - 1;
	int count = 0;

	while (src >= startPos)
	{
		buffer[dst--] = buffer[src--];
		count++;
		if (count == 3 && src >= startPos)
		{
			buffer[dst--] = ',';
			count = 0;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: formats and localizes a number for UI display
//          Shared between CLIENT and UI contexts
//-----------------------------------------------------------------------------
static SQRESULT FormatAndLocalizeNumber_Impl(HSQUIRRELVM v)
{
	const SQChar* formatStr = nullptr;
	sq_getstring(v, 2, &formatStr);

	HSQOBJECT obj;
	sq_getstackobj(v, 3, &obj);

	SQBool addThousands = SQFalse;
	sq_getbool(v, 4, &addThousands);

	const bool isInteger = (obj._type == OT_INTEGER);

	bool trimZeros = false;
	char printfFmt[64];
	ConvertRuiStyleFormatToPrintfFormat(formatStr, printfFmt, sizeof(printfFmt),
		isInteger, &trimZeros);

	char buffer[256];

	if (isInteger)
	{
		SQInteger intVal = 0;
		sq_getinteger(v, 3, &intVal);

		if (strchr(printfFmt, 'f'))
			snprintf(buffer, sizeof(buffer), printfFmt, (double)intVal);
		else
			snprintf(buffer, sizeof(buffer), printfFmt, (int)intVal);
	}
	else
	{
		SQFloat floatVal = 0.0f;
		sq_getfloat(v, 3, &floatVal);

		if (strchr(printfFmt, 'd'))
			snprintf(buffer, sizeof(buffer), printfFmt, (int)floatVal);
		else
			snprintf(buffer, sizeof(buffer), printfFmt, (double)floatVal);
	}

	if (trimZeros)
		TrimTrailingZeros(buffer);

	if (addThousands)
		InsertThousandsSeparators(buffer, sizeof(buffer));

	sq_pushstring(v, buffer, -1);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT ClientScript_FormatAndLocalizeNumber(HSQUIRRELVM v)
{
	return FormatAndLocalizeNumber_Impl(v);
}

static SQRESULT UIScript_FormatAndLocalizeNumber(HSQUIRRELVM v)
{
	return FormatAndLocalizeNumber_Impl(v);
}


//=============================================================================
// Registration
//=============================================================================

//-----------------------------------------------------------------------------
// Purpose: registers color palette + utility functions in CLIENT context
//-----------------------------------------------------------------------------
void Script_RegisterColorPaletteFunctions(CSquirrelVM* s)
{
	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, ColorPalette_RGBtoHSV,
		"Converts RGB color (0-255) to HSV (H=0-360, S=0-1, V=0-1)",
		"vector", "vector rgbColor", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, ColorPalette_HSVtoRGB,
		"Converts HSV color (H=0-360, S=0-1, V=0-1) to RGB (0-255)",
		"vector", "vector hsvColor", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, ColorPalette_ClampAndValidateColor,
		"Clamps color brightness to minimum 50%",
		"vector", "vector rgbColor", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, ColorPalette_ClampAndValidateLaserSightColor,
		"Clamps laser sight color brightness using ConVar min/max",
		"vector", "vector rgbColor", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, ColorPalette_GetDefaultColorFromID,
		"Gets default (non-overridden) color from palette by ID",
		"vector", "int colorID", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, ColorPalette_GetColorFromID,
		"Gets color from palette by ID (override if set, else default)",
		"vector", "int colorID", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, ColorPalette_GetIDFromString,
		"Gets palette index from a color name, or -1 if not found",
		"int", "string colorName", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, ColorPalette_IsColorCustomized,
		"Returns true if the palette color at this ID has been overridden",
		"bool", "int colorID", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, GetConVarColor,
		"Gets a ConVar value as an RGB color vector (0-255)",
		"vector", "string cvarName", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, SetConVarColor,
		"Sets a ConVar value from an RGB color vector (0-255)",
		"void", "string cvarName, vector rgbColor", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_SliderControl_SetCurrentValue,
		"Sets slider control value for a HUD element",
		"void", "var hudElement, float value", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_SliderControl_GetCurrentValue,
		"Gets slider control value from a HUD element",
		"float", "var hudElement", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, FormatAndLocalizeNumber,
		"Formats a number using RUI-style format string",
		"string", "string format, var number, bool addThousandsSeparator", false);

}

//-----------------------------------------------------------------------------
// Purpose: registers color palette functions in UI context
//-----------------------------------------------------------------------------
void Script_RegisterColorPaletteUIFunctions(CSquirrelVM* s)
{
	DEFINE_UI_SCRIPTFUNC_NAMED(s, ColorPalette_RGBtoHSV,
		"Converts RGB color (0-255) to HSV (H=0-360, S=0-1, V=0-1)",
		"vector", "vector rgbColor", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, ColorPalette_HSVtoRGB,
		"Converts HSV color (H=0-360, S=0-1, V=0-1) to RGB (0-255)",
		"vector", "vector hsvColor", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, ColorPalette_ClampAndValidateColor,
		"Clamps color brightness to minimum 50%",
		"vector", "vector rgbColor", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, ColorPalette_ClampAndValidateLaserSightColor,
		"Clamps laser sight color brightness using ConVar min/max",
		"vector", "vector rgbColor", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, ColorPalette_GetDefaultColorFromID,
		"Gets default (non-overridden) color from palette by ID",
		"vector", "int colorID", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, ColorPalette_GetColorFromID,
		"Gets color from palette by ID (override if set, else default)",
		"vector", "int colorID", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, ColorPalette_GetIDFromString,
		"Gets palette index from a color name, or -1 if not found",
		"int", "string colorName", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, ColorPalette_IsColorCustomized,
		"Returns true if the palette color at this ID has been overridden",
		"bool", "int colorID", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, GetConVarColor,
		"Gets a ConVar value as an RGB color vector (0-255)",
		"vector", "string cvarName", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, SetConVarColor,
		"Sets a ConVar value from an RGB color vector (0-255)",
		"void", "string cvarName, vector rgbColor", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_SliderControl_SetCurrentValue,
		"Sets slider control value for a HUD element",
		"void", "var hudElement, float value", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_SliderControl_GetCurrentValue,
		"Gets slider control value from a HUD element",
		"float", "var hudElement", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, OverrideColor,
		"Overrides a named color in the color palette",
		"void", "vector rgbColor, string colorName", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, FormatAndLocalizeNumber,
		"Formats a number using RUI-style format string",
		"string", "string format, var number, bool addThousandsSeparator", false);
}

void ColorPalette_LevelShutdown()
{
	s_sliderValues.clear();
}
