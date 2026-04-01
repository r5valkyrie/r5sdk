//=============================================================================//
// 
// Purpose: Expose native code to VScript API
// 
//-----------------------------------------------------------------------------
// 
// Read the documentation in 'game/shared/vscript_shared.cpp' before modifying
// existing code or adding new code!
// 
// To create client script bindings:
// - use the DEFINE_CLIENT_SCRIPTFUNC_NAMED() macro.
// - prefix your function with "ClientScript_" i.e.: "ClientScript_GetVersion".
// 
// To create ui script bindings:
// - use the DEFINE_UI_SCRIPTFUNC_NAMED() macro.
// - prefix your function with "UIScript_" i.e.: "UIScript_GetVersion".
// 
//=============================================================================//

#include "core/stdafx.h"
#include "tier0/frametask.h"
#include "tier1/keyvalues.h"
#include "engine/cmodel_bsp.h"
#include "engine/host_state.h"
#include "engine/debugoverlay.h"
#include "pluginsystem/pluginsystem.h"
#include "networksystem/pylon.h"
#include "networksystem/listmanager.h"
#include "networksystem/hostmanager.h"

#include "vscript/vscript.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"

#include "game/shared/vscript_gamedll_defs.h"

#include "game/shared/vscript_shared.h"
#include "game/shared/vscript_debug_overlay_shared.h"

#include "vscript_client.h"
#include "vscript_colorpalette.h"
#include "vscript_hud.h"
#include "vscript_player.h"
#include "vscript_remotefunctions.h"
#include "scriptnetdata_client.h"
#include "viewmodel_poseparam.h"
#include "rui_walltime.h"
#include "game/client/clientleafsystem.h"
#include "game/client/c_baseentity.h"
#include "game/shared/weapon_script_vars.h"
#include "game/shared/weapon_heat.h"
#include "game/shared/globalnonrewind_vars.h"
#include "game/shared/deathfield_system.h"
#include "game/shared/highlight_context.h"
#include "game/shared/status_effects_sdk.h"
#include "particle_effects_sdk.h"
#include "vscript/languages/squirrel_re/include/sqarray.h"
#include "public/globalvars_base.h"
#include "inputsystem/inputsystem.h"

extern CGlobalVarsBase* gpGlobals;

/*
=====================
SQVM_ClientScript_f

  Executes input on the
  VM in CLIENT context.
=====================
*/
static void SQVM_ClientScript_f(const CCommand& args)
{
    if (args.ArgC() >= 2)
    {
        Script_Execute(args.ArgS(), SQCONTEXT::CLIENT);
    }
}

/*
=====================
SQVM_UIScript_f

  Executes input on the
  VM in UI context.
=====================
*/
static void SQVM_UIScript_f(const CCommand& args)
{
    if (args.ArgC() >= 2)
    {
        Script_Execute(args.ArgS(), SQCONTEXT::UI);
    }
}

static ConCommand script_client("script_client", SQVM_ClientScript_f, "Run input code as CLIENT script on the VM", FCVAR_DEVELOPMENTONLY | FCVAR_CLIENTDLL | FCVAR_CHEAT);
static ConCommand script_ui("script_ui", SQVM_UIScript_f, "Run input code as UI script on the VM", FCVAR_DEVELOPMENTONLY | FCVAR_CLIENTDLL | FCVAR_CHEAT);

//-----------------------------------------------------------------------------
// Networked Variable Category System (SNDC_*)
// Engine has ScriptNetVar hash table at g_pScriptNetVarTable (56-byte entries)
// Entry: +0x00 = hash (uint64), +0x08 = category (int32)
//-----------------------------------------------------------------------------
#include "scriptnetdata_client.h"

// ScriptNetVar table entry
struct ScriptNetVarEntry_t
{
	uint64_t hash;
	int32_t category;
	char _pad[44];
};

constexpr int SCRIPTNETVAR_BUCKET_COUNT = 250;

// FNV-1a hash used by engine for netvar names
static uint64_t HashNetVarName(const char* name)
{
	uint64_t hash = 0xcbf29ce484222325ULL;
	while (*name)
	{
		hash ^= (uint8_t)*name++;
		hash *= 0x100000001b3ULL;
	}
	return hash;
}

static int GetNetVarCategoryFromTable(const char* varName)
{
	ScriptNetVarEntry_t* pTable = reinterpret_cast<ScriptNetVarEntry_t*>(g_pScriptNetVarTable);
	if (!pTable || !varName)
		return SNDC_GLOBAL;

	uint64_t hash = HashNetVarName(varName);
	int bucket = (int)(hash % SCRIPTNETVAR_BUCKET_COUNT);

	// Linear probe for collision resolution
	for (int i = 0; i < SCRIPTNETVAR_BUCKET_COUNT; i++)
	{
		int idx = (bucket + i) % SCRIPTNETVAR_BUCKET_COUNT;
		ScriptNetVarEntry_t& entry = pTable[idx];

		if (entry.hash == 0)
			break; // Empty slot, not found

		if (entry.hash == hash)
			return entry.category;
	}

	return SNDC_GLOBAL; // Not found, default
}

static SQRESULT ClientScript_GetNetworkedVariableCategory(HSQUIRRELVM v)
{
	const SQChar* varName = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &varName)) || !varName)
	{
		sq_pushinteger(v, SNDC_GLOBAL);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	int category = GetNetVarCategoryFromTable(varName);
	sq_pushinteger(v, category);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT UIScript_GetNetworkedVariableCategory(HSQUIRRELVM v)
{
	return ClientScript_GetNetworkedVariableCategory(v);
}

//-----------------------------------------------------------------------------
// Purpose: client NDebugOverlay proxies
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_DebugDrawSolidBox(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawSolidBox(v);
}
static SQRESULT ClientScript_DebugDrawSweptBox(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawSweptBox(v);
}
static SQRESULT ClientScript_DebugDrawTriangle(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawTriangle(v);
}
static SQRESULT ClientScript_DebugDrawSolidSphere(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawSolidSphere(v);
}
static SQRESULT ClientScript_DebugDrawCapsule(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawCapsule(v);
}
static SQRESULT ClientScript_CreateBox(HSQUIRRELVM v)
{
    return SharedScript_CreateBox(v);
}
static SQRESULT ClientScript_ClearBoxes(HSQUIRRELVM v)
{
    return SharedScript_ClearBoxes(v);
}

//-----------------------------------------------------------------------------
// Purpose: internal handler for adding debug texts on screen through scripts
//-----------------------------------------------------------------------------
static void ClientScript_Internal_DebugScreenTextWithColor(HSQUIRRELVM v, const float posX, const float posY, const Color color, const char* const text)
{
    g_pDebugOverlay->AddScreenTextOverlay(posX, posY, NDEBUG_PERSIST_TILL_NEXT_CLIENT, color.r(), color.g(), color.b(), color.a(), text);
}

//-----------------------------------------------------------------------------
// Purpose: adds a debug text on the screen at given position
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_DebugScreenText(HSQUIRRELVM v)
{
    if (g_pDebugOverlay)
    {
        SQFloat posX;
        SQFloat posY;
        const SQChar* text;

        sq_getfloat(v, 2, &posX);
        sq_getfloat(v, 3, &posY);
        sq_getstring(v, 4, &text);

        const Color color(255, 255, 255, 255);

        ClientScript_Internal_DebugScreenTextWithColor(v, posX, posY, color, text);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: adds a debug text on the screen at given position with color
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_DebugScreenTextWithColor(HSQUIRRELVM v)
{
    if (g_pDebugOverlay)
    {
        SQFloat posX;
        SQFloat posY;
        const SQChar* text;
        const SQVector3D* colorVec;

        sq_getfloat(v, 2, &posX);
        sq_getfloat(v, 3, &posY);
        sq_getstring(v, 4, &text);
        sq_getvector(v, 5, &colorVec);

        const Color color = Script_VectorToColor(colorVec, 1.0f);
        ClientScript_Internal_DebugScreenTextWithColor(v, posX, posY, color, text);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets the current number of visible objects
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_GetVisibleObjectCount(HSQUIRRELVM v)
{
	sq_pushinteger(v, static_cast<SQInteger>(ClientLeafSystem_GetVisibleObjectCount()));
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets the maximum number of visible objects (8191)
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_GetVisibleObjectMax(HSQUIRRELVM v)
{
	sq_pushinteger(v, static_cast<SQInteger>(ClientLeafSystem_GetVisibleObjectMax()));
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets the visible object budget threshold
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_GetVisibleObjectBudget(HSQUIRRELVM v)
{
	sq_pushinteger(v, static_cast<SQInteger>(ClientLeafSystem_GetVisibleObjectBudget()));
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: returns whether the visible object system is in overflow
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_IsVisibleObjectOverflow(HSQUIRRELVM v)
{
	sq_pushbool(v, ClientLeafSystem_IsOverflowing());
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets the most recent server id
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_GetServerID(HSQUIRRELVM v)
{
    sq_pushstring(v, Host_GetSessionID(), -1);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Engine function typedefs for GetElementsByClassname and GetParentMenu.
// The native implementations only support UI context; these reimplementations
// add CLIENT context support by calling the same engine functions with the
// appropriate scriptHud global (g_clientScriptHud vs g_uiScriptHud).
//-----------------------------------------------------------------------------
typedef uintptr_t(__fastcall* HscriptToMenuFn)(SQObject*);
typedef uintptr_t(__fastcall* HudElementForPanelFn)(uintptr_t, uintptr_t);
typedef void(__fastcall* ScriptErrorFn)(const char*);

static uintptr_t s_engineBase = 0;

static uintptr_t EngineBase()
{
    if (!s_engineBase)
        s_engineBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    return s_engineBase;
}

// sub_1407861B0: converts script handle SQObject to C++ menu/panel pointer
static HscriptToMenuFn GetHscriptToMenu()
{
    static HscriptToMenuFn fn = reinterpret_cast<HscriptToMenuFn>(EngineBase() + 0x7861B0);
    return fn;
}

// sub_14098E380: looks up HudElement for a given panel in a scriptHud
static HudElementForPanelFn GetHudElementForPanel()
{
    static HudElementForPanelFn fn = reinterpret_cast<HudElementForPanelFn>(EngineBase() + 0x98E380);
    return fn;
}

// sub_1408DDB70: raises a script error message (non-fatal)
static ScriptErrorFn GetScriptError()
{
    static ScriptErrorFn fn = reinterpret_cast<ScriptErrorFn>(EngineBase() + 0x8DDB70);
    return fn;
}

// qword_14D4752A0: g_clientScriptHud (CLIENT context)
static uintptr_t GetClientScriptHud()
{
    return *reinterpret_cast<uintptr_t*>(EngineBase() + 0xD4752A0);
}

// qword_14D423B18: g_uiScriptHud (UI context)
static uintptr_t GetUIScriptHud()
{
    return *reinterpret_cast<uintptr_t*>(EngineBase() + 0xD423B18);
}

// qword_14D40B358: g_pVGuiPanel
static uintptr_t GetVGuiPanel()
{
    return *reinterpret_cast<uintptr_t*>(EngineBase() + 0xD40B358);
}

// qword_14D40B3B0: g_pVGuiInput
static uintptr_t GetVGuiInput()
{
    return *reinterpret_cast<uintptr_t*>(EngineBase() + 0xD40B3B0);
}

// SDK-managed menu stack
static std::vector<uintptr_t> s_menuStack;

//-----------------------------------------------------------------------------
// Purpose: returns the currently focused HUD element (CLIENT context)
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_GetFocus(HSQUIRRELVM v)
{
    const uintptr_t clientScriptHud = GetClientScriptHud();
    if (!clientScriptHud)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    const uintptr_t vguiInput = GetVGuiInput();
    const uintptr_t vguiPanel = GetVGuiPanel();
    if (!vguiInput || !vguiPanel)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Call g_pVGuiInput->GetFocus() - vtable offset +0x20
    typedef uintptr_t(__fastcall* GetFocusFn)(uintptr_t);
    GetFocusFn pfnGetFocus = reinterpret_cast<GetFocusFn>(
        *reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(vguiInput) + 0x20));
    uintptr_t focusHandle = pfnGetFocus(vguiInput);

    if (!focusHandle)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Call g_pVGuiPanel->GetPanel(focusHandle, "ClientDLL") - vtable offset +624
    typedef uintptr_t(__fastcall* GetPanelFn)(uintptr_t, uintptr_t, const char*);
    GetPanelFn pfnGetPanel = reinterpret_cast<GetPanelFn>(
        *reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(vguiPanel) + 624));
    uintptr_t panel = pfnGetPanel(vguiPanel, focusHandle, "ClientDLL");

    if (!panel)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Convert to script handle via HudElementForPanel
    uintptr_t hudElement = GetHudElementForPanel()(clientScriptHud, panel);
    if (!hudElement)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Push the SQObject stored at hudElement+48
    SQObject* storedObj = *reinterpret_cast<SQObject**>(hudElement + 48);
    if (storedObj && storedObj->_type == OT_INSTANCE && storedObj->_unVal.pInstance)
    {
        sq_pushobject(v, *storedObj);
    }
    else
    {
        sq_pushnull(v);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: returns the currently focused HUD element (UI context)
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetFocus(HSQUIRRELVM v)
{
    const uintptr_t uiScriptHud = GetUIScriptHud();
    if (!uiScriptHud)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    const uintptr_t vguiInput = GetVGuiInput();
    const uintptr_t vguiPanel = GetVGuiPanel();
    if (!vguiInput || !vguiPanel)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Call g_pVGuiInput->GetFocus() - vtable offset +0x20
    typedef uintptr_t(__fastcall* GetFocusFn)(uintptr_t);
    GetFocusFn pfnGetFocus = reinterpret_cast<GetFocusFn>(
        *reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(vguiInput) + 0x20));
    uintptr_t focusHandle = pfnGetFocus(vguiInput);

    if (!focusHandle)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Call g_pVGuiPanel->GetPanel(focusHandle, "UIDLL") - vtable offset +624
    typedef uintptr_t(__fastcall* GetPanelFn)(uintptr_t, uintptr_t, const char*);
    GetPanelFn pfnGetPanel = reinterpret_cast<GetPanelFn>(
        *reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(vguiPanel) + 624));
    uintptr_t panel = pfnGetPanel(vguiPanel, focusHandle, "UIDLL");

    if (!panel)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Convert to script handle via HudElementForPanel
    uintptr_t hudElement = GetHudElementForPanel()(uiScriptHud, panel);
    if (!hudElement)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Push the SQObject stored at hudElement+48
    SQObject* storedObj = *reinterpret_cast<SQObject**>(hudElement + 48);
    if (storedObj && storedObj->_type == OT_INSTANCE && storedObj->_unVal.pInstance)
    {
        sq_pushobject(v, *storedObj);
    }
    else
    {
        sq_pushnull(v);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: sets the mouse cursor position (CLIENT context)
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_SetCursorPosition(HSQUIRRELVM v)
{
    const SQVector3D* pos;
    sq_getvector(v, 2, &pos);

    if (g_pInputSystem)
    {
        g_pInputSystem->SetCursorPosition(static_cast<int>(pos->x), static_cast<int>(pos->y));
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: sets the mouse cursor position (UI context)
//-----------------------------------------------------------------------------
static SQRESULT UIScript_SetCursorPosition(HSQUIRRELVM v)
{
    const SQVector3D* pos;
    sq_getvector(v, 2, &pos);

    if (g_pInputSystem)
    {
        g_pInputSystem->SetCursorPosition(static_cast<int>(pos->x), static_cast<int>(pos->y));
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets the mouse cursor position (CLIENT/UI shared)
//-----------------------------------------------------------------------------
static SQRESULT Script_GetCursorPosition_Impl(HSQUIRRELVM v)
{
    int x = 0, y = 0;
    if (g_pInputSystem)
    {
        g_pInputSystem->GetCursorPosition(&x, &y);
    }

    // Return as vector with z=0
    SQVector3D result(static_cast<SQFloat>(x), static_cast<SQFloat>(y), 0.0f);
    sq_pushvector(v, &result);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_GetCursorPosition(HSQUIRRELVM v) { return Script_GetCursorPosition_Impl(v); }
static SQRESULT UIScript_GetCursorPosition(HSQUIRRELVM v) { return Script_GetCursorPosition_Impl(v); }

//-----------------------------------------------------------------------------
// Purpose: returns the HUD element under the mouse cursor (CLIENT context)
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_GetMouseFocus(HSQUIRRELVM v)
{
    const uintptr_t clientScriptHud = GetClientScriptHud();
    if (!clientScriptHud)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    const uintptr_t vguiInput = GetVGuiInput();
    const uintptr_t vguiPanel = GetVGuiPanel();
    if (!vguiInput || !vguiPanel)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Call g_pVGuiInput->GetMouseFocus() - vtable offset +0x28 (follows GetFocus at +0x20)
    typedef uintptr_t(__fastcall* GetMouseFocusFn)(uintptr_t);
    GetMouseFocusFn pfnGetMouseFocus = reinterpret_cast<GetMouseFocusFn>(
        *reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(vguiInput) + 0x28));
    uintptr_t focusHandle = pfnGetMouseFocus(vguiInput);

    if (!focusHandle)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Call g_pVGuiPanel->GetPanel(focusHandle, "ClientDLL") - vtable offset +624
    typedef uintptr_t(__fastcall* GetPanelFn)(uintptr_t, uintptr_t, const char*);
    GetPanelFn pfnGetPanel = reinterpret_cast<GetPanelFn>(
        *reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(vguiPanel) + 624));
    uintptr_t panel = pfnGetPanel(vguiPanel, focusHandle, "ClientDLL");

    if (!panel)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Convert to script handle via HudElementForPanel
    uintptr_t hudElement = GetHudElementForPanel()(clientScriptHud, panel);
    if (!hudElement)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Push the SQObject stored at hudElement+48
    SQObject* storedObj = *reinterpret_cast<SQObject**>(hudElement + 48);
    if (storedObj && storedObj->_type == OT_INSTANCE && storedObj->_unVal.pInstance)
    {
        sq_pushobject(v, *storedObj);
    }
    else
    {
        sq_pushnull(v);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: returns the HUD element under the mouse cursor (UI context)
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetMouseFocus(HSQUIRRELVM v)
{
    const uintptr_t uiScriptHud = GetUIScriptHud();
    if (!uiScriptHud)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    const uintptr_t vguiInput = GetVGuiInput();
    const uintptr_t vguiPanel = GetVGuiPanel();
    if (!vguiInput || !vguiPanel)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Call g_pVGuiInput->GetMouseFocus() - vtable offset +0x28
    typedef uintptr_t(__fastcall* GetMouseFocusFn)(uintptr_t);
    GetMouseFocusFn pfnGetMouseFocus = reinterpret_cast<GetMouseFocusFn>(
        *reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(vguiInput) + 0x28));
    uintptr_t focusHandle = pfnGetMouseFocus(vguiInput);

    if (!focusHandle)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Call g_pVGuiPanel->GetPanel(focusHandle, "UIDLL") - vtable offset +624
    typedef uintptr_t(__fastcall* GetPanelFn)(uintptr_t, uintptr_t, const char*);
    GetPanelFn pfnGetPanel = reinterpret_cast<GetPanelFn>(
        *reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(vguiPanel) + 624));
    uintptr_t panel = pfnGetPanel(vguiPanel, focusHandle, "UIDLL");

    if (!panel)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Convert to script handle via HudElementForPanel
    uintptr_t hudElement = GetHudElementForPanel()(uiScriptHud, panel);
    if (!hudElement)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Push the SQObject stored at hudElement+48
    SQObject* storedObj = *reinterpret_cast<SQObject**>(hudElement + 48);
    if (storedObj && storedObj->_type == OT_INSTANCE && storedObj->_unVal.pInstance)
    {
        sq_pushobject(v, *storedObj);
    }
    else
    {
        sq_pushnull(v);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets a list of elements within the given menu matching classname
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_GetElementsByClassname(HSQUIRRELVM v)
{
    const uintptr_t base = EngineBase();

    // Get menu handle from Squirrel stack (arg 1 = stack index 2)
    SQObject menuObj;
    if (SQ_FAILED(sq_getstackobj(v, 2, &menuObj)))
    {
        v_SQVM_RaiseError(v, "Failed to get menu argument");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Get classname string from Squirrel stack (arg 2 = stack index 3)
    const SQChar* classname = nullptr;
    if (SQ_FAILED(sq_getstring(v, 3, &classname)))
    {
        v_SQVM_RaiseError(v, "Failed to get classname argument");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Create empty result array on the Squirrel stack
    sq_newarray(v, 0);

    // Convert script handle to menu pointer (sub_1407861B0)
    SQObject* menuPtr = (menuObj._type == OT_NULL) ? nullptr : &menuObj;
    uintptr_t menu = GetHscriptToMenu()(menuPtr);

    if (!menu)
    {
        GetScriptError()("Not a menu");
        GetScriptError()("GetElementsByClassname: invalid menu");
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK); // return empty array
    }

    // Validate menu type: menu->vtable[46]() must match expected type
    uintptr_t menuVtable = *reinterpret_cast<uintptr_t*>(menu);
    uintptr_t expectedType = *reinterpret_cast<uintptr_t*>(base + 0xD4DCFE8);

    typedef uintptr_t(__fastcall* VtableFn)(uintptr_t);
    VtableFn pfnGetMenuType = reinterpret_cast<VtableFn>(
        *reinterpret_cast<uintptr_t*>(menuVtable + 368));

    if (pfnGetMenuType(menu) != expectedType)
    {
        GetScriptError()("Not a menu");
        GetScriptError()("GetElementsByClassname: invalid menu");
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Get panels for classname via menu->vtable[238](menu, classname, &panelList)
    uintptr_t panelList = 0;
    typedef int(__fastcall* GetPanelsFn)(uintptr_t, const char*, uintptr_t*);
    GetPanelsFn pfnGetPanels = reinterpret_cast<GetPanelsFn>(
        *reinterpret_cast<uintptr_t*>(menuVtable + 1904));

    int count = pfnGetPanels(menu, classname, &panelList);

    if (count > 0)
    {
        // Validate clientScriptHud is available
        uintptr_t clientScriptHud = GetClientScriptHud();
        if (!clientScriptHud)
        {
            SCRIPT_CHECK_AND_RETURN(v, SQ_OK); // return empty array
        }

        // g_pVGuiPanel (qword_14D40B358) panel system for resolving handles
        uintptr_t panelSystem = *reinterpret_cast<uintptr_t*>(base + 0xD40B358);
        if (!panelSystem)
        {
            SCRIPT_CHECK_AND_RETURN(v, SQ_OK); // return empty array
        }

        uintptr_t panelSysVtable = *reinterpret_cast<uintptr_t*>(panelSystem);

        typedef uintptr_t(__fastcall* GetPanelFn)(uintptr_t, uintptr_t, const char*);
        GetPanelFn pfnGetPanel = reinterpret_cast<GetPanelFn>(
            *reinterpret_cast<uintptr_t*>(panelSysVtable + 624));

        for (int i = 0; i < count; i++)
        {
            uintptr_t panelHandle = *reinterpret_cast<uintptr_t*>(panelList + 8 * i);
            uintptr_t panel = pfnGetPanel(panelSystem, panelHandle, "ClientDLL");
            if (!panel)
                continue;

            // Look up HudElement in CLIENT scriptHud (sub_14098E380)
            uintptr_t hudElement = GetHudElementForPanel()(clientScriptHud, panel);
            if (!hudElement)
                continue;

            // hudElement+48 contains a POINTER to an SQObject (allocated by sub_141056680)
            SQObject* storedObj = *reinterpret_cast<SQObject**>(hudElement + 48);
            if (storedObj && storedObj->_type == OT_INSTANCE && storedObj->_unVal.pInstance)
            {
                sq_pushobject(v, *storedObj);
                sq_arrayappend(v, -2);
            }
        }
    }

    // Array is on top of stack as return value
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets the menu that the element is contained in
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_GetParentMenu(HSQUIRRELVM v)
{
    // Get element handle from Squirrel stack (arg 1 = stack index 2)
    SQObject elemObj;
    if (SQ_FAILED(sq_getstackobj(v, 2, &elemObj)))
    {
        v_SQVM_RaiseError(v, "Failed to get element argument");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Validate clientScriptHud is available before any lookups
    const uintptr_t clientScriptHud = GetClientScriptHud();
    if (!clientScriptHud)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Convert script handle to C++ object pointer (sub_1407861B0)
    SQObject* elemPtr = (elemObj._type == OT_NULL) ? nullptr : &elemObj;
    uintptr_t obj = GetHscriptToMenu()(elemPtr);

    if (!obj)
    {
        GetScriptError()("Argument is not a panel");
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Read parent panel pointer at offset 568 (0x238)
    uintptr_t panel = *reinterpret_cast<uintptr_t*>(obj + 568);
    if (!panel)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Look up HudElement in CLIENT scriptHud (sub_14098E380)
    uintptr_t hudElement = GetHudElementForPanel()(clientScriptHud, panel);
    if (!hudElement)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // hudElement+48 contains a POINTER to an SQObject (allocated by sub_141056680)
    SQObject* storedObj = *reinterpret_cast<SQObject**>(hudElement + 48);
    if (storedObj && storedObj->_type == OT_INSTANCE && storedObj->_unVal.pInstance)
    {
        sq_pushobject(v, *storedObj);
    }
    else
    {
        sq_pushnull(v);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets a list of elements within the given menu matching classname (UI)
// Note: UI context version using g_uiScriptHud.
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetElementsByClassname(HSQUIRRELVM v)
{
    const uintptr_t base = EngineBase();

    // Get menu handle from Squirrel stack (arg 1 = stack index 2)
    SQObject menuObj;
    if (SQ_FAILED(sq_getstackobj(v, 2, &menuObj)))
    {
        v_SQVM_RaiseError(v, "Failed to get menu argument");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Get classname string from Squirrel stack (arg 2 = stack index 3)
    const SQChar* classname = nullptr;
    if (SQ_FAILED(sq_getstring(v, 3, &classname)))
    {
        v_SQVM_RaiseError(v, "Failed to get classname argument");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Create empty result array on the Squirrel stack
    sq_newarray(v, 0);

    // Convert script handle to menu pointer (sub_1407861B0)
    SQObject* menuPtr = (menuObj._type == OT_NULL) ? nullptr : &menuObj;
    uintptr_t menu = GetHscriptToMenu()(menuPtr);

    if (!menu)
    {
        GetScriptError()("Not a menu");
        GetScriptError()("GetElementsByClassname: invalid menu");
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK); // return empty array
    }

    // Validate menu type: menu->vtable[46]() must match expected type
    uintptr_t menuVtable = *reinterpret_cast<uintptr_t*>(menu);
    uintptr_t expectedType = *reinterpret_cast<uintptr_t*>(base + 0xD4DCFE8);

    typedef uintptr_t(__fastcall* VtableFn)(uintptr_t);
    VtableFn pfnGetMenuType = reinterpret_cast<VtableFn>(
        *reinterpret_cast<uintptr_t*>(menuVtable + 368));

    if (pfnGetMenuType(menu) != expectedType)
    {
        GetScriptError()("Not a menu");
        GetScriptError()("GetElementsByClassname: invalid menu");
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Get panels for classname via menu->vtable[238](menu, classname, &panelList)
    uintptr_t panelList = 0;
    typedef int(__fastcall* GetPanelsFn)(uintptr_t, const char*, uintptr_t*);
    GetPanelsFn pfnGetPanels = reinterpret_cast<GetPanelsFn>(
        *reinterpret_cast<uintptr_t*>(menuVtable + 1904));

    int count = pfnGetPanels(menu, classname, &panelList);

    if (count > 0)
    {
        // Use UI scriptHud for UI context
        uintptr_t uiScriptHud = GetUIScriptHud();
        if (!uiScriptHud)
        {
            SCRIPT_CHECK_AND_RETURN(v, SQ_OK); // return empty array
        }

        // g_pVGuiPanel (qword_14D40B358) panel system for resolving handles
        uintptr_t panelSystem = *reinterpret_cast<uintptr_t*>(base + 0xD40B358);
        if (!panelSystem)
        {
            SCRIPT_CHECK_AND_RETURN(v, SQ_OK); // return empty array
        }

        uintptr_t panelSysVtable = *reinterpret_cast<uintptr_t*>(panelSystem);

        typedef uintptr_t(__fastcall* GetPanelFn)(uintptr_t, uintptr_t, const char*);
        GetPanelFn pfnGetPanel = reinterpret_cast<GetPanelFn>(
            *reinterpret_cast<uintptr_t*>(panelSysVtable + 624));

        for (int i = 0; i < count; i++)
        {
            uintptr_t panelHandle = *reinterpret_cast<uintptr_t*>(panelList + 8 * i);
            uintptr_t panel = pfnGetPanel(panelSystem, panelHandle, "UIDLL");
            if (!panel)
                continue;

            // Look up HudElement in UI scriptHud (sub_14098E380)
            uintptr_t hudElement = GetHudElementForPanel()(uiScriptHud, panel);
            if (!hudElement)
                continue;

            // hudElement+48 contains a POINTER to an SQObject (allocated by sub_141056680)
            SQObject* storedObj = *reinterpret_cast<SQObject**>(hudElement + 48);
            if (storedObj && storedObj->_type == OT_INSTANCE && storedObj->_unVal.pInstance)
            {
                sq_pushobject(v, *storedObj);
                sq_arrayappend(v, -2);
            }
        }
    }

    // Array is on top of stack as return value
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets the menu that the element is contained in (UI context)
// Note: UI context version using g_uiScriptHud.
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetParentMenu(HSQUIRRELVM v)
{
    // Get element handle from Squirrel stack (arg 1 = stack index 2)
    SQObject elemObj;
    if (SQ_FAILED(sq_getstackobj(v, 2, &elemObj)))
    {
        v_SQVM_RaiseError(v, "Failed to get element argument");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Use UI scriptHud for UI context
    const uintptr_t uiScriptHud = GetUIScriptHud();
    if (!uiScriptHud)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Convert script handle to C++ object pointer (sub_1407861B0)
    SQObject* elemPtr = (elemObj._type == OT_NULL) ? nullptr : &elemObj;
    uintptr_t obj = GetHscriptToMenu()(elemPtr);

    if (!obj)
    {
        GetScriptError()("Argument is not a panel");
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Read parent panel pointer at offset 568 (0x238)
    uintptr_t panel = *reinterpret_cast<uintptr_t*>(obj + 568);
    if (!panel)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Look up HudElement in UI scriptHud (sub_14098E380)
    uintptr_t hudElement = GetHudElementForPanel()(uiScriptHud, panel);
    if (!hudElement)
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // hudElement+48 contains a POINTER to an SQObject (allocated by sub_141056680)
    SQObject* storedObj = *reinterpret_cast<SQObject**>(hudElement + 48);
    if (storedObj && storedObj->_type == OT_INSTANCE && storedObj->_unVal.pInstance)
    {
        sq_pushobject(v, *storedObj);
    }
    else
    {
        sq_pushnull(v);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Helper: push a menu element (from raw pointer) onto the Squirrel stack
//-----------------------------------------------------------------------------
static void PushMenuElement(HSQUIRRELVM v, uintptr_t scriptHud, uintptr_t menuPtr)
{
    uintptr_t hudElement = GetHudElementForPanel()(scriptHud, menuPtr);
    if (!hudElement)
    {
        sq_pushnull(v);
        return;
    }

    SQObject* storedObj = *reinterpret_cast<SQObject**>(hudElement + 48);
    if (storedObj && storedObj->_type == OT_INSTANCE && storedObj->_unVal.pInstance)
        sq_pushobject(v, *storedObj);
    else
        sq_pushnull(v);
}

//-----------------------------------------------------------------------------
// Helper: extract menu pointer from Squirrel stack slot 2
//-----------------------------------------------------------------------------
static uintptr_t ExtractMenuFromStack(HSQUIRRELVM v)
{
    SQObject menuObj;
    if (SQ_FAILED(sq_getstackobj(v, 2, &menuObj)))
        return 0;

    SQObject* menuPtr = (menuObj._type == OT_NULL) ? nullptr : &menuObj;
    return GetHscriptToMenu()(menuPtr);
}

//-----------------------------------------------------------------------------
// MenuStack_Contains [UI] - checks if a menu is in the stack
//-----------------------------------------------------------------------------
static SQRESULT UIScript_MenuStack_Contains(HSQUIRRELVM v)
{
    uintptr_t menu = ExtractMenuFromStack(v);
    if (!menu)
    {
        sq_pushbool(v, SQFalse);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    for (size_t i = 0; i < s_menuStack.size(); i++)
    {
        if (s_menuStack[i] == menu)
        {
            sq_pushbool(v, SQTrue);
            SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
        }
    }

    sq_pushbool(v, SQFalse);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// MenuStack_GetLength [UI] - returns number of menus on the stack
//-----------------------------------------------------------------------------
static SQRESULT UIScript_MenuStack_GetLength(HSQUIRRELVM v)
{
    sq_pushinteger(v, static_cast<SQInteger>(s_menuStack.size()));
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// MenuStack_Push [UI] - push a menu onto the top of the stack
//-----------------------------------------------------------------------------
static SQRESULT UIScript_MenuStack_Push(HSQUIRRELVM v)
{
    uintptr_t menu = ExtractMenuFromStack(v);
    if (!menu)
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

    s_menuStack.push_back(menu);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// MenuStack_Remove [UI] - remove a menu from the stack by value
//-----------------------------------------------------------------------------
static SQRESULT UIScript_MenuStack_Remove(HSQUIRRELVM v)
{
    uintptr_t menu = ExtractMenuFromStack(v);
    if (!menu)
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

    for (auto it = s_menuStack.begin(); it != s_menuStack.end(); ++it)
    {
        if (*it == menu)
        {
            s_menuStack.erase(it);
            break;
        }
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// MenuStack_Pop [CLIENT|UI] - remove and return top menu
//-----------------------------------------------------------------------------
static SQRESULT MenuStack_Pop_Internal(HSQUIRRELVM v, uintptr_t scriptHud)
{
    if (s_menuStack.empty())
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    uintptr_t elem = s_menuStack.back();
    s_menuStack.pop_back();
    PushMenuElement(v, scriptHud, elem);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT UIScript_MenuStack_Pop(HSQUIRRELVM v)
{
    return MenuStack_Pop_Internal(v, GetUIScriptHud());
}

static SQRESULT ClientScript_MenuStack_Pop(HSQUIRRELVM v)
{
    return MenuStack_Pop_Internal(v, GetClientScriptHud());
}

//-----------------------------------------------------------------------------
// MenuStack_Top [CLIENT|UI] - return top menu without removing
//-----------------------------------------------------------------------------
static SQRESULT MenuStack_Top_Internal(HSQUIRRELVM v, uintptr_t scriptHud)
{
    if (s_menuStack.empty())
    {
        sq_pushnull(v);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    PushMenuElement(v, scriptHud, s_menuStack.back());
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT UIScript_MenuStack_Top(HSQUIRRELVM v)
{
    return MenuStack_Top_Internal(v, GetUIScriptHud());
}

static SQRESULT ClientScript_MenuStack_Top(HSQUIRRELVM v)
{
    return MenuStack_Top_Internal(v, GetClientScriptHud());
}

//-----------------------------------------------------------------------------
// MenuStack_GetCopy [CLIENT|UI] - return array copy of the stack
//-----------------------------------------------------------------------------
static SQRESULT MenuStack_GetCopy_Internal(HSQUIRRELVM v, uintptr_t scriptHud)
{
    sq_newarray(v, 0);

    for (size_t i = 0; i < s_menuStack.size(); i++)
    {
        PushMenuElement(v, scriptHud, s_menuStack[i]);
        sq_arrayappend(v, -2);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT UIScript_MenuStack_GetCopy(HSQUIRRELVM v)
{
    return MenuStack_GetCopy_Internal(v, GetUIScriptHud());
}

static SQRESULT ClientScript_MenuStack_GetCopy(HSQUIRRELVM v)
{
    return MenuStack_GetCopy_Internal(v, GetClientScriptHud());
}

//-----------------------------------------------------------------------------
// Purpose: returns the current client game time
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_ClientTime(HSQUIRRELVM v)
{
    sq_pushfloat(v, gpGlobals->curTime);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: returns the current client game time (UI context)
//-----------------------------------------------------------------------------
static SQRESULT UIScript_ClientTime(HSQUIRRELVM v)
{
    sq_pushfloat(v, gpGlobals->curTime);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: returns the current UI time (real wall-clock time)
//-----------------------------------------------------------------------------
static SQRESULT UIScript_UITime(HSQUIRRELVM v)
{
    sq_pushfloat(v, static_cast<SQFloat>(Plat_FloatTime()));
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: filters an entity array by proximity to a position
//          returns entities within range, up to 100
//-----------------------------------------------------------------------------
static constexpr int ENTITY_USERPOINTER_OFFSET = 0x38;
static constexpr int CBASEENTITY_M_IEFLAGS_OFFSET = 0x58;
static constexpr int CBASEENTITY_M_VECABSORIGIN_OFFSET = 0x14C;
static constexpr int EFL_KILLME = 0x1;
static constexpr int MAX_NEARBY_ENTITIES = 100;

static SQRESULT ClientScript_GetEntitiesFromArrayNearPos(HSQUIRRELVM v)
{
    SQObject arrObj;
    if (SQ_FAILED(sq_getstackobj(v, 2, &arrObj)) || !sq_isarray(arrObj))
    {
        v_SQVM_RaiseError(v, "Expected array as first argument");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    const SQVector3D* pos = nullptr;
    if (SQ_FAILED(sq_getvector(v, 3, &pos)) || !pos)
    {
        v_SQVM_RaiseError(v, "Expected vector as second argument");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    SQFloat range = 0.0f;
    if (SQ_FAILED(sq_getfloat(v, 4, &range)))
    {
        v_SQVM_RaiseError(v, "Expected float as third argument");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    SQArray* pArray = _array(arrObj);
    const SQInteger count = pArray->Size();

    if (count > 0)
    {
        SQObjectPtr firstElem;
        pArray->Get(0, firstElem);
        if (firstElem._type != OT_ENTITY)
        {
            v_SQVM_RaiseError(v, "Expected array of entities");
            SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
        }
    }

    const float rangeSq = range * range;
    C_BaseEntity* nearbyEnts[MAX_NEARBY_ENTITIES];
    int nearbyCount = 0;

    for (SQInteger i = 0; i < count; i++)
    {
        SQObjectPtr elem;
        if (!pArray->Get(i, elem) || elem._type != OT_ENTITY)
            continue;

        C_BaseEntity* pEnt = *reinterpret_cast<C_BaseEntity**>(
            reinterpret_cast<uintptr_t>(elem._unVal.pInstance) + ENTITY_USERPOINTER_OFFSET);

        if (!pEnt)
            continue;

        const int eflags = *reinterpret_cast<int*>(
            reinterpret_cast<char*>(pEnt) + CBASEENTITY_M_IEFLAGS_OFFSET);

        if (eflags & EFL_KILLME)
            continue;

        const Vector3D& entOrigin = *reinterpret_cast<Vector3D*>(
            reinterpret_cast<char*>(pEnt) + CBASEENTITY_M_VECABSORIGIN_OFFSET);

        const float dx = entOrigin.x - pos->x;
        const float dy = entOrigin.y - pos->y;
        const float dz = entOrigin.z - pos->z;
        const float distSq = dx * dx + dy * dy + dz * dz;

        if (distSq < rangeSq)
        {
            nearbyEnts[nearbyCount++] = pEnt;
            if (nearbyCount >= MAX_NEARBY_ENTITIES)
                break;
        }
    }

    sq_newarray(v, 0);

    for (int i = 0; i < nearbyCount; i++)
    {
        const HSCRIPT scriptHandle = v_C_BaseEntity__GetScriptInstance(nearbyEnts[i]);
        if (!scriptHandle)
            continue;

        SQObject entObj;
        entObj._type = OT_ENTITY;
        entObj._pad = 0;
        entObj._unVal.pInstance = reinterpret_cast<SQInstance*>(scriptHandle);
        sq_pushobject(v, entObj);
        sq_arrayappend(v, -2);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: checks if the server index is valid, raises an error if not
//-----------------------------------------------------------------------------
static SQBool Script_CheckServerIndexAndFailure(HSQUIRRELVM v, SQInteger iServer)
{
    SQInteger iCount = static_cast<SQInteger>(g_ServerListManager.m_vServerList.size());

    if (iServer >= iCount)
    {
        v_SQVM_RaiseError(v, "Index must be less than %i.\n", iCount);
        return false;
    }
    else if (iServer == -1) // If its still -1, then 'sq_getinteger' failed
    {
        v_SQVM_RaiseError(v, "Invalid argument type provided.\n");
        return false;
    }

    return true;
}

static void Internal_UIScript_RequestForServerBrowserListThreaded()
{
    string responseMsg;
    size_t serverCount;

    const bool success = g_ServerListManager.RefreshServerList(responseMsg, serverCount);

    g_TaskQueue.Dispatch([success, errorMsg = std::move(responseMsg), serverCount]
        {
            if (!g_pUIScript)
                return;

            HSCRIPT onRequestComplete = g_pUIScript->FindFunction("UICodeCallback_OnServerListRequestCompleted",
                "void functionref( bool success, string errorMsg, int serverCount )", nullptr);

            if (!onRequestComplete)
                return;

            ScriptVariant_t args[3] = { success, errorMsg.c_str(), (int)serverCount };
            g_pUIScript->ExecuteFunction(onRequestComplete, args, SDK_ARRAYSIZE(args), nullptr, 0);

            free(onRequestComplete);
        }, 0);
}

//-----------------------------------------------------------------------------
// Purpose: refreshes the server list
//-----------------------------------------------------------------------------
static SQRESULT UIScript_RequestServerList(HSQUIRRELVM v)
{
    std::thread(Internal_UIScript_RequestForServerBrowserListThreaded).detach();
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: get current server count from pylon
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetServerCount(HSQUIRRELVM v)
{
    size_t iCount = g_ServerListManager.m_vServerList.size();
    sq_pushinteger(v, static_cast<SQInteger>(iCount));

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: get response from private server request
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetHiddenServerName(HSQUIRRELVM v)
{
    const SQChar* privateToken = nullptr;

    if (SQ_FAILED(sq_getstring(v, 2, &privateToken)) || VALID_CHARSTAR(privateToken))
    {
        v_SQVM_ScriptError("Empty or null private token");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    string hiddenServerRequestMessage;
    NetGameServer_t serverListing;

    bool result = g_MasterServer.GetServerByToken(serverListing, hiddenServerRequestMessage, privateToken); // Send token connect request.
    if (!result)
    {
        if (hiddenServerRequestMessage.empty())
            sq_pushstring(v, "Request failed", -1);
        else
        {
            hiddenServerRequestMessage = Format("Request failed: %s", hiddenServerRequestMessage.c_str());
            sq_pushstring(v, hiddenServerRequestMessage.c_str(), (SQInteger)hiddenServerRequestMessage.length());
        }

        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    if (serverListing.name.empty())
    {
        if (hiddenServerRequestMessage.empty())
            hiddenServerRequestMessage = Format("Server listing empty");
        else
            hiddenServerRequestMessage = Format("Server listing empty: %s", hiddenServerRequestMessage.c_str());

        sq_pushstring(v, hiddenServerRequestMessage.c_str(), (SQInteger)hiddenServerRequestMessage.length());
    }
    else
    {
        hiddenServerRequestMessage = Format("Found server: %s", serverListing.name.c_str());
        sq_pushstring(v, hiddenServerRequestMessage.c_str(), (SQInteger)hiddenServerRequestMessage.length());
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: get server's current name from server list index
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetServerName(HSQUIRRELVM v)
{
    AUTO_LOCK(g_ServerListManager.m_Mutex);

    SQInteger iServer = -1;
    sq_getinteger(v, 2, &iServer);

    if (!Script_CheckServerIndexAndFailure(v, iServer))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const string& serverName = g_ServerListManager.m_vServerList[iServer].name;
    sq_pushstring(v, serverName.c_str(), (SQInteger)serverName.length());

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: get server's current description from server list index
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetServerDescription(HSQUIRRELVM v)
{
    AUTO_LOCK(g_ServerListManager.m_Mutex);

    SQInteger iServer = -1;
    sq_getinteger(v, 2, &iServer);

    if (!Script_CheckServerIndexAndFailure(v, iServer))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const string& serverDescription = g_ServerListManager.m_vServerList[iServer].description;
    sq_pushstring(v, serverDescription.c_str(), (SQInteger)serverDescription.length());

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: get server's current map via server list index
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetServerMap(HSQUIRRELVM v)
{
    AUTO_LOCK(g_ServerListManager.m_Mutex);

    SQInteger iServer = -1;
    sq_getinteger(v, 2, &iServer);

    if (!Script_CheckServerIndexAndFailure(v, iServer))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const string& serverMapName = g_ServerListManager.m_vServerList[iServer].map;
    sq_pushstring(v, serverMapName.c_str(), (SQInteger)serverMapName.length());

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: get server's current playlist via server list index
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetServerPlaylist(HSQUIRRELVM v)
{
    AUTO_LOCK(g_ServerListManager.m_Mutex);

    SQInteger iServer = -1;
    sq_getinteger(v, 2, &iServer);

    if (!Script_CheckServerIndexAndFailure(v, iServer))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const string& serverPlaylist = g_ServerListManager.m_vServerList[iServer].playlist;
    sq_pushstring(v, serverPlaylist.c_str(), (SQInteger)serverPlaylist.length());

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: get server's current player count via server list index
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetServerCurrentPlayers(HSQUIRRELVM v)
{
    AUTO_LOCK(g_ServerListManager.m_Mutex);

    SQInteger iServer = -1;
    sq_getinteger(v, 2, &iServer);

    if (!Script_CheckServerIndexAndFailure(v, iServer))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const SQInteger playerCount = g_ServerListManager.m_vServerList[iServer].numPlayers;
    sq_pushinteger(v, playerCount);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: get server's current player count via server list index
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetServerMaxPlayers(HSQUIRRELVM v)
{
    AUTO_LOCK(g_ServerListManager.m_Mutex);

    SQInteger iServer = -1;
    sq_getinteger(v, 2, &iServer);

    if (!Script_CheckServerIndexAndFailure(v, iServer))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const SQInteger maxPlayers = g_ServerListManager.m_vServerList[iServer].maxPlayers;
    sq_pushinteger(v, maxPlayers);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT UIScript_GetServerHasPassword(HSQUIRRELVM v)
{
    AUTO_LOCK(g_ServerListManager.m_Mutex);

    SQInteger iServer = -1;
    sq_getinteger(v, 2, &iServer);

    if (!Script_CheckServerIndexAndFailure(v, iServer))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const SQBool hasPassword = g_ServerListManager.m_vServerList[iServer].hasPassword;
    sq_pushbool(v, hasPassword);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets the most recent server id
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetServerID(HSQUIRRELVM v)
{
    sq_pushstring(v, Host_GetSessionID(), -1);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: get promo data for server browser panels
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetPromoData(HSQUIRRELVM v)
{
    enum class R5RPromoData : SQInteger
    {
        PromoLargeTitle,
        PromoLargeDesc,
        PromoLeftTitle,
        PromoLeftDesc,
        PromoRightTitle,
        PromoRightDesc
    };

    SQInteger idx = 0;
    sq_getinteger(v, 2, &idx);

    R5RPromoData ePromoIndex = static_cast<R5RPromoData>(idx);
    const char* pszPromoKey;

    switch (ePromoIndex)
    {
    case R5RPromoData::PromoLargeTitle:
    {
        pszPromoKey = "#PROMO_LARGE_TITLE";
        break;
    }
    case R5RPromoData::PromoLargeDesc:
    {
        pszPromoKey = "#PROMO_LARGE_DESCRIPTION";
        break;
    }
    case R5RPromoData::PromoLeftTitle:
    {
        pszPromoKey = "#PROMO_LEFT_TITLE";
        break;
    }
    case R5RPromoData::PromoLeftDesc:
    {
        pszPromoKey = "#PROMO_LEFT_DESCRIPTION";
        break;
    }
    case R5RPromoData::PromoRightTitle:
    {
        pszPromoKey = "#PROMO_RIGHT_TITLE";
        break;
    }
    case R5RPromoData::PromoRightDesc:
    {
        pszPromoKey = "#PROMO_RIGHT_DESCRIPTION";
        break;
    }
    default:
    {
        pszPromoKey = "#PROMO_SDK_ERROR";
        break;
    }
    }

    sq_pushstring(v, pszPromoKey, -1);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static void Internal_UIScript_RequestEULAThreaded()
{
    MSEulaData_t eulaDataMs;
    string responseMsg;

    const bool success = g_MasterServer.GetEULA(eulaDataMs, responseMsg);

    g_TaskQueue.Dispatch([success, errorMsg = std::move(responseMsg), eulaData = std::move(eulaDataMs)]
        {
            if (!g_pUIScript)
                return;

            HSCRIPT onRequestComplete = g_pUIScript->FindFunction("UICodeCallback_OnEULARequestCompleted",
                "void functionref( bool success, string errorMsg, string language, string eulaData )", nullptr);

            if (!onRequestComplete)
                return;

            ScriptVariant_t args[4] = { success, errorMsg.c_str(), eulaData.language.c_str(), eulaData.contents.c_str() };
            g_pUIScript->ExecuteFunction(onRequestComplete, args, SDK_ARRAYSIZE(args), nullptr, 0);

            free(onRequestComplete);
        }, 0);
}

static SQRESULT UIScript_RequestEULAContents(HSQUIRRELVM v)
{
    std::thread(Internal_UIScript_RequestEULAThreaded).detach();
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: connect to server from native server browser entries
//-----------------------------------------------------------------------------
static SQRESULT UIScript_ConnectToServer(HSQUIRRELVM v)
{
    const SQChar* ipAddress = nullptr;
    if (SQ_FAILED(sq_getstring(v, 2, &ipAddress)))
    {
        v_SQVM_ScriptError("Missing ip address");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    const SQChar* cryptoKey = nullptr;
    if (SQ_FAILED(sq_getstring(v, 3, &cryptoKey)))
    {
        v_SQVM_ScriptError("Missing encryption key");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    const SQChar* password = nullptr;
    if (SQ_FAILED(sq_getstring(v, 4, &password)))
    {
        password = "";
    }

    Msg(eDLL_T::UI, "Connecting to server with ip address '%s' and encryption key '%s'\n", ipAddress, cryptoKey);
    g_ServerListManager.ConnectToServer(ipAddress, cryptoKey, password);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: set netchannel encryption key and connect to server
//-----------------------------------------------------------------------------
static SQRESULT UIScript_ConnectToListedServer(HSQUIRRELVM v)
{
    AUTO_LOCK(g_ServerListManager.m_Mutex);

    SQInteger iServer = -1;
    sq_getinteger(v, 2, &iServer);

    const SQChar* password = nullptr;
    if (SQ_FAILED(sq_getstring(v, 4, &password)))
    {
        password = "";
    }

    if (!Script_CheckServerIndexAndFailure(v, iServer))
    {
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    const NetGameServer_t& gameServer = g_ServerListManager.m_vServerList[iServer];

    g_ServerListManager.ConnectToServer(gameServer.address, gameServer.port, gameServer.netKey, password);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: request token from pylon and join server with result.
//-----------------------------------------------------------------------------
static SQRESULT UIScript_ConnectToHiddenServer(HSQUIRRELVM v)
{
    const SQChar* privateToken = nullptr;
    const SQRESULT strRet = sq_getstring(v, 2, &privateToken);

    if (SQ_FAILED(strRet) || VALID_CHARSTAR(privateToken))
    {
        v_SQVM_ScriptError("Empty or null private token");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    string hiddenServerRequestMessage;
    NetGameServer_t netListing;

    const bool result = g_MasterServer.GetServerByToken(netListing, hiddenServerRequestMessage, privateToken); // Send token connect request.
    if (result)
    {
        g_ServerListManager.ConnectToServer(netListing.address, netListing.port, netListing.netKey, "");
    }
    else
    {
        Warning(eDLL_T::UI, "Failed to connect to private server: %s\n", hiddenServerRequestMessage.c_str());
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: create server via native server browser entries
// TODO: return a boolean on failure instead of raising an error, so we could
// determine from scripts whether or not to spin a local server, or connect
// to a dedicated server (for disconnecting and loading the lobby, for example)
//-----------------------------------------------------------------------------
static SQRESULT UIScript_CreateServer(HSQUIRRELVM v)
{
    const SQChar* serverName = nullptr;
    const SQChar* serverDescription = nullptr;
    const SQChar* serverMapName = nullptr;
    const SQChar* serverPlaylist = nullptr;

    sq_getstring(v, 2, &serverName);
    sq_getstring(v, 3, &serverDescription);
    sq_getstring(v, 4, &serverMapName);
    sq_getstring(v, 5, &serverPlaylist);

    SQInteger serverVisibility = 0;
    sq_getinteger(v, 6, &serverVisibility);

    if (!VALID_CHARSTAR(serverName) ||
        !VALID_CHARSTAR(serverMapName) ||
        !VALID_CHARSTAR(serverPlaylist))
    {
        v_SQVM_ScriptError("Empty or null server criteria");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    hostname->SetValue(serverName);
    hostdesc.SetValue(serverDescription);

    pylon_host_visibility.SetValue((int)serverVisibility);

    // Launch server.
    g_ServerHostManager.LaunchServer(serverMapName, serverPlaylist);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: shuts the server down and disconnects all clients
//-----------------------------------------------------------------------------
static SQRESULT UIScript_DestroyServer(HSQUIRRELVM v)
{
    if (g_pHostState->m_bActiveGame)
        g_pHostState->m_iNextState = HostStates_t::HS_GAME_SHUTDOWN;

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//---------------------------------------------------------------------------------
// Purpose: registers script functions in CLIENT context
// Input  : *s -
//---------------------------------------------------------------------------------
void Script_RegisterClientFunctions(CSquirrelVM* s)
{
    Script_RegisterCommonAbstractions(s);
    Script_RegisterCoreClientFunctions(s);

    // NOTE: plugin functions must always come after SDK functions!
    for (auto& callback : !PluginSystem()->GetRegisterClientScriptFuncsCallbacks())
    {
        // Register script functions inside plugins.
        callback.Function()(s);
    }
}

//---------------------------------------------------------------------------------
// Purpose: core client script functions
// Input  : *s - 
//---------------------------------------------------------------------------------
void Script_RegisterCoreClientFunctions(CSquirrelVM* s)
{
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, DebugDrawSolidBox, "Draw a debug overlay solid box", "void", "vector origin, vector mins, vector maxs, vector color, float alpha, bool drawThroughWorld, float duration", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, DebugDrawSweptBox, "Draw a debug overlay swept box", "void", "vector start, vector end, vector mins, vector maxs, vector angles, vector color, float alpha, bool drawThroughWorld, float duration", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, DebugDrawTriangle, "Draw a debug overlay triangle", "void", "vector p1, vector p2, vector p3, vector color, float alpha, bool drawThroughWorld, float duration", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, DebugDrawSolidSphere, "Draw a debug overlay solid sphere", "void", "vector origin, float radius, int theta, int phi, vector color, float alpha, bool drawThroughWorld, float duration", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, DebugDrawCapsule, "Draw a debug overlay capsule", "void", "vector start, vector end, float radius, vector color, float alpha, bool drawThroughWorld, float duration", false);

    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, CreateBox, "Create a permanent box for map making", "void", "vector origin, vector angles, vector mins, vector maxs, vector color, float alpha", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, ClearBoxes, "Clear all debug overlays and boxes", "void", "", false);

    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, GetServerID, "Gets the ID of the most recent server", "string", "", false);

    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, GetVisibleObjectCount, "Gets the current number of visible objects being rendered", "int", "", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, GetVisibleObjectMax, "Gets the maximum number of visible objects (8191)", "int", "", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, GetVisibleObjectBudget, "Gets the visible object budget threshold", "int", "", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, IsVisibleObjectOverflow, "Returns true if the visible object system is in overflow", "bool", "", false);

    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, ClientTime, "Returns the current client time", "float", "", false);
	
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, GetElementsByClassname, "Gets a list of elements within the given menu matching a classname", "array< var >", "var menu, string classname", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, GetParentMenu, "Gets the menu that the element is contained in", "var", "var elem", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, MenuStack_GetCopy, "Returns a copy of the menu stack", "array< var >", "", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, MenuStack_Pop, "Removes and returns the top menu from the menu stack", "var", "", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, MenuStack_Top, "Returns the top menu from the menu stack without removing", "var", "", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, GetEntitiesFromArrayNearPos, "Filters entity array by proximity to a position", "array< entity >", "array< entity > entities, vector pos, float range", false);

    Script_RegisterColorPaletteFunctions(s);
    Script_RegisterHudFunctions(s);
    Script_RegisterRemoteFunctionNatives(s);

    s->RegisterConstant("SHIELD_CHANGE_SOURCE_DIRECT", 0);
    s->RegisterConstant("SHIELD_CHANGE_SOURCE_REGEN", 1);
    s->RegisterConstant("FX_PATTACH_WEAPON_CHARGE_FRACTION_CURVED", 0x18);
    s->RegisterConstant("FORCE_STANCE_STAND", 0);
    s->RegisterConstant("FORCE_STANCE_CROUCH", 1);
    s->RegisterConstant("WT_GADGET", 9);

    s->RegisterConstant("PHASETYPE_DEFAULT", 0);
    s->RegisterConstant("PHASETYPE_BALANCE", 1);
    s->RegisterConstant("PHASETYPE_TUNNEL", 2);
    s->RegisterConstant("PHASETYPE_DASH", 3);
    s->RegisterConstant("PHASETYPE_GATE", 4);
    s->RegisterConstant("PHASETYPE_BREACH", 5);
    s->RegisterConstant("PHASETYPE_TRANSPORT", 6);
    s->RegisterConstant("PHASETYPE_DOOR", 7);
    s->RegisterConstant("PHASETYPE_TELEPORTER", 8);
    s->RegisterConstant("PHASETYPE_REWIND", 9);

    s->RegisterConstant("INFINITEAMMO_NONE", 0);
    s->RegisterConstant("INFINITEAMMO_CLIPS", 1);

    s->RegisterConstant("HIGHLIGHT_FLAG_REQUIRE_SAME_TEAM", 0x80);
    s->RegisterConstant("HIGHLIGHT_FLAG_REQUIRE_DIFFERENT_TEAM", 0x100);
    s->RegisterConstant("HIGHLIGHT_FLAG_REQUIRE_FRIENDLY_TEAM", 0x200);
    s->RegisterConstant("HIGHLIGHT_FLAG_REQUIRE_ENEMY_TEAM", 0x400);
    s->RegisterConstant("HIGHLIGHT_FLAG_REQUIRE_LOW_MOVEMENT", 0x1000);
    s->RegisterConstant("HIGHLIGHT_FLAG_REQUIRE_HIGH_MOVEMENT", 0x2000);
    s->RegisterConstant("HIGHLIGHT_FLAG_CHECK_OFTEN", 0x4000);
    s->RegisterConstant("HIGHLIGHT_FLAG_CHECK_NEXT_FRAME", 0x8000);
    s->RegisterConstant("HIGHLIGHT_FLAG_DISABLE_DEATH_FADE", 0x10000);
    s->RegisterConstant("HIGHLIGHT_FLAG_TEAM_AGNOSTIC", 0x20000);
    s->RegisterConstant("HIGHLIGHT_FLAG_ADDITIONAL_LOS_CHECKS", 0x80000);
    s->RegisterConstant("HIGHLIGHT_VIS_LOS_ENTSONLY_BLOCKSCAN", 7);

    HighlightContext_RegisterDrawFuncEnum(s->GetVM());

    Script_RegisterFuncNamed(s, "HighlightContext_GetId", "Script_HighlightContext_GetId", "Get highlight context id by name", "int", "string name", false, Script_HighlightContext_GetId);
    Script_RegisterFuncNamed(s, "HighlightContext_SetParam", "Script_HighlightContext_SetParam", "Set highlight param", "void", "int contextId, int paramIndex, vector value", false, Script_HighlightContext_SetParam);
    Script_RegisterFuncNamed(s, "HighlightContext_GetParam", "Script_HighlightContext_GetParam", "Get highlight param", "vector", "int contextId, int paramIndex", false, Script_HighlightContext_GetParam);
    Script_RegisterFuncNamed(s, "HighlightContext_SetDrawFunc", "Script_HighlightContext_SetDrawFunc", "Set draw function", "void", "int contextId, int drawFuncId", false, Script_HighlightContext_SetDrawFunc);
    Script_RegisterFuncNamed(s, "HighlightContext_GetDrawFunc", "Script_HighlightContext_GetDrawFunc", "Get draw function", "int", "int contextId", false, Script_HighlightContext_GetDrawFunc);
    Script_RegisterFuncNamed(s, "HighlightContext_SetRadius", "Script_HighlightContext_SetRadius", "Set outline radius", "void", "int contextId, float radius", false, Script_HighlightContext_SetRadius);
    Script_RegisterFuncNamed(s, "HighlightContext_GetOutlineRadius", "Script_HighlightContext_GetOutlineRadius", "Get outline radius", "float", "int contextId", false, Script_HighlightContext_GetOutlineRadius);
    Script_RegisterFuncNamed(s, "HighlightContext_GetInsideFunction", "Script_HighlightContext_GetInsideFunction", "Get inside function", "int", "int contextId", false, Script_HighlightContext_GetInsideFunction);
    Script_RegisterFuncNamed(s, "HighlightContext_GetOutlineFunction", "Script_HighlightContext_GetOutlineFunction", "Get outline function", "int", "int contextId", false, Script_HighlightContext_GetOutlineFunction);
    Script_RegisterFuncNamed(s, "HighlightContext_SetFlags", "Script_HighlightContext_SetFlags", "Set flags", "void", "int contextId, int flags", false, Script_HighlightContext_SetFlags);
    Script_RegisterFuncNamed(s, "HighlightContext_SetNearFadeDistance", "Script_HighlightContext_SetNearFadeDistance", "Set near fade distance", "void", "int contextId, float distance", false, Script_HighlightContext_SetNearFadeDistance);
    Script_RegisterFuncNamed(s, "HighlightContext_SetFarFadeDistance", "Script_HighlightContext_SetFarFadeDistance", "Set far fade distance", "void", "int contextId, float distance", false, Script_HighlightContext_SetFarFadeDistance);
    Script_RegisterFuncNamed(s, "HighlightContext_SetFocusedColor", "Script_HighlightContext_SetFocusedColor", "Set focused color", "void", "int contextId, vector color", false, Script_HighlightContext_SetFocusedColor);
    Script_RegisterFuncNamed(s, "HighlightContext_IsEntityVisible", "Script_HighlightContext_IsEntityVisible", "Is entity visible", "bool", "int contextId", false, Script_HighlightContext_IsEntityVisible);
    Script_RegisterFuncNamed(s, "HighlightContext_IsAfterPostProcess", "Script_HighlightContext_IsAfterPostProcess", "Is after post process", "bool", "int contextId", false, Script_HighlightContext_IsAfterPostProcess);

    // Weapon base class lookup (global functions)
    Script_RegisterFuncNamed(s, "Weapon_GetBaseClassName", "Script_Global_Weapon_GetBaseClassName", "Returns baseclass or the weapon's classname", "string", "string weaponClassName", false, Script_Global_Weapon_GetBaseClassName);
    Script_RegisterFuncNamed(s, "Weapon_GetBaseClassNameOrEmpty", "Script_Global_Weapon_GetBaseClassNameOrEmpty", "Returns baseclass or empty string", "string", "string weaponClassName", false, Script_Global_Weapon_GetBaseClassNameOrEmpty);

    ScriptNetData_RegisterClientFunctions(s);

    // GlobalNonRewind variable system - getters only for CLIENT VM (setters are SERVER-only)
    Script_RegisterFuncNamed(s, "GetGlobalNonRewindNetBool", "Script_GetGlobalNonRewindNetBool", "Gets a global non-rewind bool", "bool", "string name", false, Script_GetGlobalNonRewindNetBool);
    Script_RegisterFuncNamed(s, "GetGlobalNonRewindNetInt", "Script_GetGlobalNonRewindNetInt", "Gets a global non-rewind int", "int", "string name", false, Script_GetGlobalNonRewindNetInt);
    Script_RegisterFuncNamed(s, "GetGlobalNonRewindNetFloat", "Script_GetGlobalNonRewindNetFloat", "Gets a global non-rewind float", "float", "string name", false, Script_GetGlobalNonRewindNetFloat);
    Script_RegisterFuncNamed(s, "GetGlobalNonRewindNetTime", "Script_GetGlobalNonRewindNetTime", "Gets a global non-rewind time", "float", "string name", false, Script_GetGlobalNonRewindNetTime);
    Script_RegisterFuncNamed(s, "GetGlobalNonRewindNetEnt", "Script_GetGlobalNonRewindNetEnt", "Gets a global non-rewind entity", "entity ornull", "string name", false, Script_GetGlobalNonRewindNetEnt);


    DeathField_RegisterOnVM(s);

    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, GetNetworkedVariableCategory, "Gets the category of a registered networked variable", "int", "string varName", false);

    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, GetFocus, "Returns the currently focused HUD element", "var", "", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, GetMouseFocus, "Returns the HUD element under the mouse cursor", "var", "", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, SetCursorPosition, "Sets the mouse cursor position", "void", "vector pos", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, GetCursorPosition, "Gets the mouse cursor position as a vector", "vector", "", false);

    RuiWallTime_RegisterNatives(s);
    StatusEffects_SDK_RegisterClientFunctions(s);

    // Particle effect functions (EffectRestart, EffectSetDistanceCullingScalar)
    ParticleEffects_SDK_RegisterClientFunctions(s);
}


//---------------------------------------------------------------------------------
// Purpose: registers script functions in UI context
// Input  : *s -
//---------------------------------------------------------------------------------
void Script_RegisterUIFunctions(CSquirrelVM* s)
{
    // Register entity/player classes in UI VM so that entity method syntax
    // (e.g. player.GetPlayerNetInt("name")) compiles without errors.
    // The engine only registers these classes for CLIENT/SERVER VMs.
    // Methods are lazily resolved from the class descriptors.
    if (v_RegisterScriptClass && g_clientScriptEntityStruct && g_clientScriptPlayerStruct)
    {
        HSQUIRRELVM v = s->GetVM();
        v_RegisterScriptClass(v, "ClientEntityStruct", "e",
            g_clientScriptEntityStruct, nullptr);
        v_RegisterScriptClass(v, "ClientPlayerStruct", "p",
            g_clientScriptPlayerStruct, g_clientScriptEntityStruct);
        DevMsg(eDLL_T::CLIENT, "[VScriptUI] Registered entity/player classes in UI VM\n");
    }

    Script_RegisterCommonAbstractions(s);

    DEFINE_UI_SCRIPTFUNC_NAMED(s, ClientTime, "Returns the current client time", "float", "", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, UITime, "Returns the current UI time", "float", "", false);

    DEFINE_UI_SCRIPTFUNC_NAMED(s, RequestServerList, "Requests the latest public server list, calls UICodeCallback_OnServerListRequestCompleted on completion", "void", "", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetServerCount, "Gets the number of public servers", "int", "", false);

    // Functions for retrieving server browser data
    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetHiddenServerName, "Gets hidden server name by token", "string", "string token", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetServerName, "Gets the name of the server at the specified index of the server list", "string", "int index", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetServerDescription, "Gets the description of the server at the specified index of the server list", "string", "int index", false);

    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetServerMap, "Gets the map of the server at the specified index of the server list", "string", "int index", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetServerPlaylist, "Gets the playlist of the server at the specified index of the server list", "string", "int index", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetServerCurrentPlayers, "Gets the current player count of the server at the specified index of the server list", "int", "int index", false);

    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetServerMaxPlayers, "Gets the max player count of the server at the specified index of the server list", "int", "int index", false);

    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetServerHasPassword, "Gets the current hasPassword bool at the specified index of the server list", "bool", "int index", false);

    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetServerID, "Gets the ID of the most recent server", "string", "", false);

    // Misc main menu functions
    DEFINE_UI_SCRIPTFUNC_NAMED(s, RequestEULAContents, "Requests the latest online EULA contents, calls UICodeCallback_OnEULARequestCompleted on completion", "void", "", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetPromoData, "Gets promo data for specified slot type", "string", "int slotType", false);

    // Functions for connecting to servers
    DEFINE_UI_SCRIPTFUNC_NAMED(s, ConnectToServer, "Joins server by ip address and encryption key", "void", "string address, string key", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, ConnectToListedServer, "Joins listed server by index", "void", "int index", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, ConnectToHiddenServer, "Joins hidden server by token", "void", "string token", false);

    // UI element functions (using g_uiScriptHud)
    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetElementsByClassname, "Gets a list of elements within the given menu matching a classname", "array< var >", "var menu, string classname", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetParentMenu, "Gets the menu that the element is contained in", "var", "var elem", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, MenuStack_Contains, "Checks if a menu is in the menu stack", "bool", "var menu", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, MenuStack_GetLength, "Returns the number of menus on the menu stack", "int", "", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, MenuStack_Push, "Push a menu onto the top of the menu stack", "void", "var menu", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, MenuStack_Remove, "Removes a menu from the menu stack", "void", "var menu", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, MenuStack_GetCopy, "Returns a copy of the menu stack", "array< var >", "", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, MenuStack_Pop, "Removes and returns the top menu from the menu stack", "var", "", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, MenuStack_Top, "Returns the top menu from the menu stack without removing", "var", "", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetFocus, "Returns the currently focused HUD element", "var", "", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetMouseFocus, "Returns the HUD element under the mouse cursor", "var", "", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, SetCursorPosition, "Sets the mouse cursor position", "void", "vector pos", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetCursorPosition, "Gets the mouse cursor position as a vector", "vector", "", false);

    Script_RegisterColorPaletteUIFunctions(s);
    Script_RegisterHudUIFunctions(s);
    Script_RegisterRemoteFunctionUINatives(s);

    s->RegisterConstant("SHIELD_CHANGE_SOURCE_DIRECT", 0);
    s->RegisterConstant("SHIELD_CHANGE_SOURCE_REGEN", 1);
    s->RegisterConstant("FX_PATTACH_WEAPON_CHARGE_FRACTION_CURVED", 0x18);
    s->RegisterConstant("FORCE_STANCE_STAND", 0);
    s->RegisterConstant("FORCE_STANCE_CROUCH", 1);
    s->RegisterConstant("WT_GADGET", 9);

    s->RegisterConstant("PHASETYPE_DEFAULT", 0);
    s->RegisterConstant("PHASETYPE_BALANCE", 1);
    s->RegisterConstant("PHASETYPE_TUNNEL", 2);
    s->RegisterConstant("PHASETYPE_DASH", 3);
    s->RegisterConstant("PHASETYPE_GATE", 4);
    s->RegisterConstant("PHASETYPE_BREACH", 5);
    s->RegisterConstant("PHASETYPE_TRANSPORT", 6);
    s->RegisterConstant("PHASETYPE_DOOR", 7);
    s->RegisterConstant("PHASETYPE_TELEPORTER", 8);
    s->RegisterConstant("PHASETYPE_REWIND", 9);

    s->RegisterConstant("INFINITEAMMO_NONE", 0);
    s->RegisterConstant("INFINITEAMMO_CLIPS", 1);

    s->RegisterConstant("HIGHLIGHT_FLAG_REQUIRE_SAME_TEAM", 0x80);
    s->RegisterConstant("HIGHLIGHT_FLAG_REQUIRE_DIFFERENT_TEAM", 0x100);
    s->RegisterConstant("HIGHLIGHT_FLAG_REQUIRE_FRIENDLY_TEAM", 0x200);
    s->RegisterConstant("HIGHLIGHT_FLAG_REQUIRE_ENEMY_TEAM", 0x400);
    s->RegisterConstant("HIGHLIGHT_FLAG_REQUIRE_LOW_MOVEMENT", 0x1000);
    s->RegisterConstant("HIGHLIGHT_FLAG_REQUIRE_HIGH_MOVEMENT", 0x2000);
    s->RegisterConstant("HIGHLIGHT_FLAG_CHECK_OFTEN", 0x4000);
    s->RegisterConstant("HIGHLIGHT_FLAG_CHECK_NEXT_FRAME", 0x8000);
    s->RegisterConstant("HIGHLIGHT_FLAG_DISABLE_DEATH_FADE", 0x10000);
    s->RegisterConstant("HIGHLIGHT_FLAG_TEAM_AGNOSTIC", 0x20000);
    s->RegisterConstant("HIGHLIGHT_FLAG_ADDITIONAL_LOS_CHECKS", 0x80000);
    s->RegisterConstant("HIGHLIGHT_VIS_LOS_ENTSONLY_BLOCKSCAN", 7);

    HighlightContext_RegisterDrawFuncEnum(s->GetVM());

    Script_RegisterFuncNamed(s, "HighlightContext_GetId", "Script_HighlightContext_GetId", "Get highlight context id by name", "int", "string name", false, Script_HighlightContext_GetId);
    Script_RegisterFuncNamed(s, "HighlightContext_SetParam", "Script_HighlightContext_SetParam", "Set highlight param", "void", "int contextId, int paramIndex, vector value", false, Script_HighlightContext_SetParam);
    Script_RegisterFuncNamed(s, "HighlightContext_GetParam", "Script_HighlightContext_GetParam", "Get highlight param", "vector", "int contextId, int paramIndex", false, Script_HighlightContext_GetParam);
    Script_RegisterFuncNamed(s, "HighlightContext_SetDrawFunc", "Script_HighlightContext_SetDrawFunc", "Set draw function", "void", "int contextId, int drawFuncId", false, Script_HighlightContext_SetDrawFunc);
    Script_RegisterFuncNamed(s, "HighlightContext_GetDrawFunc", "Script_HighlightContext_GetDrawFunc", "Get draw function", "int", "int contextId", false, Script_HighlightContext_GetDrawFunc);
    Script_RegisterFuncNamed(s, "HighlightContext_SetRadius", "Script_HighlightContext_SetRadius", "Set outline radius", "void", "int contextId, float radius", false, Script_HighlightContext_SetRadius);
    Script_RegisterFuncNamed(s, "HighlightContext_GetOutlineRadius", "Script_HighlightContext_GetOutlineRadius", "Get outline radius", "float", "int contextId", false, Script_HighlightContext_GetOutlineRadius);
    Script_RegisterFuncNamed(s, "HighlightContext_GetInsideFunction", "Script_HighlightContext_GetInsideFunction", "Get inside function", "int", "int contextId", false, Script_HighlightContext_GetInsideFunction);
    Script_RegisterFuncNamed(s, "HighlightContext_GetOutlineFunction", "Script_HighlightContext_GetOutlineFunction", "Get outline function", "int", "int contextId", false, Script_HighlightContext_GetOutlineFunction);
    Script_RegisterFuncNamed(s, "HighlightContext_SetFlags", "Script_HighlightContext_SetFlags", "Set flags", "void", "int contextId, int flags", false, Script_HighlightContext_SetFlags);
    Script_RegisterFuncNamed(s, "HighlightContext_SetNearFadeDistance", "Script_HighlightContext_SetNearFadeDistance", "Set near fade distance", "void", "int contextId, float distance", false, Script_HighlightContext_SetNearFadeDistance);
    Script_RegisterFuncNamed(s, "HighlightContext_SetFarFadeDistance", "Script_HighlightContext_SetFarFadeDistance", "Set far fade distance", "void", "int contextId, float distance", false, Script_HighlightContext_SetFarFadeDistance);
    Script_RegisterFuncNamed(s, "HighlightContext_SetFocusedColor", "Script_HighlightContext_SetFocusedColor", "Set focused color", "void", "int contextId, vector color", false, Script_HighlightContext_SetFocusedColor);
    Script_RegisterFuncNamed(s, "HighlightContext_IsEntityVisible", "Script_HighlightContext_IsEntityVisible", "Is entity visible", "bool", "int contextId", false, Script_HighlightContext_IsEntityVisible);
    Script_RegisterFuncNamed(s, "HighlightContext_IsAfterPostProcess", "Script_HighlightContext_IsAfterPostProcess", "Is after post process", "bool", "int contextId", false, Script_HighlightContext_IsAfterPostProcess);

    Script_RegisterFuncNamed(s, "Weapon_GetBaseClassName", "Script_Global_Weapon_GetBaseClassName", "Returns baseclass or the weapon's classname", "string", "string weaponClassName", false, Script_Global_Weapon_GetBaseClassName);
    Script_RegisterFuncNamed(s, "Weapon_GetBaseClassNameOrEmpty", "Script_Global_Weapon_GetBaseClassNameOrEmpty", "Returns baseclass or empty string", "string", "string weaponClassName", false, Script_Global_Weapon_GetBaseClassNameOrEmpty);

    ScriptNetData_RegisterUIFunctions(s);

    // GlobalNonRewind variable system - getters only for UI VM (setters are SERVER-only)
    Script_RegisterFuncNamed(s, "GetGlobalNonRewindNetBool", "Script_GetGlobalNonRewindNetBool", "Gets a global non-rewind bool", "bool", "string name", false, Script_GetGlobalNonRewindNetBool);
    Script_RegisterFuncNamed(s, "GetGlobalNonRewindNetInt", "Script_GetGlobalNonRewindNetInt", "Gets a global non-rewind int", "int", "string name", false, Script_GetGlobalNonRewindNetInt);
    Script_RegisterFuncNamed(s, "GetGlobalNonRewindNetFloat", "Script_GetGlobalNonRewindNetFloat", "Gets a global non-rewind float", "float", "string name", false, Script_GetGlobalNonRewindNetFloat);
    Script_RegisterFuncNamed(s, "GetGlobalNonRewindNetTime", "Script_GetGlobalNonRewindNetTime", "Gets a global non-rewind time", "float", "string name", false, Script_GetGlobalNonRewindNetTime);
    Script_RegisterFuncNamed(s, "GetGlobalNonRewindNetEnt", "Script_GetGlobalNonRewindNetEnt", "Gets a global non-rewind entity", "entity ornull", "string name", false, Script_GetGlobalNonRewindNetEnt);


    DeathField_RegisterOnVM(s);

    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetNetworkedVariableCategory, "Gets the category of a registered networked variable", "int", "string varName", false);

    RuiWallTime_RegisterNatives(s);
    StatusEffects_SDK_RegisterUIFunctions(s);

    // Weapon info natives - reuse engine's CLIENT/SERVER handlers for UI VM
    // Script wrappers (IsWeaponKeyFieldDefined, *_GlobalString, *_GlobalInt, etc.)
    // are defined in sh_utility_all.gnut and call through to these base natives.
    if (v_GetWeaponInfoFileKeyField_Global)
        Script_RegisterFuncNamed(s, "GetWeaponInfoFileKeyField_Global", "Native_GetWeaponInfoFileKeyField_Global",
            "Gets weapon info field by weapon name and key", "var", "string weapon, string field", false,
            v_GetWeaponInfoFileKeyField_Global);
    if (v_GetWeaponInfoFileKeyField_WithMods_Global)
        Script_RegisterFuncNamed(s, "GetWeaponInfoFileKeyField_WithMods_Global", "Native_GetWeaponInfoFileKeyField_WithMods_Global",
            "Gets weapon info field with mods by weapon name", "var", "string weapon, string mods, string field", false,
            v_GetWeaponInfoFileKeyField_WithMods_Global);
    if (v_GetWeaponInfoFileKeyFieldAsset_Global)
        Script_RegisterFuncNamed(s, "GetWeaponInfoFileKeyFieldAsset_Global", "Native_GetWeaponInfoFileKeyFieldAsset_Global",
            "Gets weapon info asset field by weapon name and key", "asset", "string weapon, string field", false,
            v_GetWeaponInfoFileKeyFieldAsset_Global);

    // NOTE: plugin functions must always come after SDK functions!
    for (auto& callback : !PluginSystem()->GetRegisterUIScriptFuncsCallbacks())
    {
        // Register script functions inside plugins.
        callback.Function()(s);
    }
}

void Script_RegisterUIServerFunctions(CSquirrelVM* s)
{
    DEFINE_UI_SCRIPTFUNC_NAMED(s, CreateServer, "Starts server with the specified settings", "void", "string name, string description, string levelName, string playlistName, int visibilityMode", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, DestroyServer, "Shuts the local server down", "void", "", false);
}

//---------------------------------------------------------------------------------
// Purpose: console variables for scripts, these should not be used in engine/sdk code !!!
//---------------------------------------------------------------------------------
static ConVar settings_reflex("settings_reflex", "1", FCVAR_RELEASE, "Selected NVIDIA Reflex mode.", "0 = Off. 1 = On. 2 = On + Boost.");
static ConVar settings_antilag("settings_antilag", "1", FCVAR_RELEASE, "Selected AMD Anti-Lag mode.", "0 = Off. 1 = On.");

// NOTE: if we want to make a certain promo only show once, add the playerprofile flag to the cvar below. Current behavior = always show after game restart.
static ConVar promo_version_accepted("promo_version_accepted", "0", FCVAR_RELEASE, "The accepted promo version.");

static ConVar customMatch_enabled("customMatch_enabled", "0", FCVAR_RELEASE, "Enable custom match features.");
static ConVar gladCards_debug("gladCards_debug", "0", FCVAR_RELEASE, "Enable gladiator card debug logging.");

//---------------------------------------------------------------------------------
// Purpose: returns true if entity has a model (model index != 0)
//---------------------------------------------------------------------------------
static SQRESULT Script_HasModel(HSQUIRRELVM v)
{
    void* pEntity = nullptr;
    if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pEntity)))
        return SQ_ERROR;

    // m_nModelIndex at offset 0x60 for client entities
    const short modelIndex = *reinterpret_cast<short*>(reinterpret_cast<char*>(pEntity) + 0x60);
    sq_pushbool(v, modelIndex != 0);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//---------------------------------------------------------------------------------
// Purpose: sets the lighting origin for a dynamic prop entity (stub)
//---------------------------------------------------------------------------------
static SQRESULT Script_SetLightingOrigin(HSQUIRRELVM v)
{
    void* pEntity = nullptr;
    if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pEntity)))
        return SQ_ERROR;

    const SQVector3D* pVec = nullptr;
    sq_getvector(v, 2, &pVec);

    if (!pVec)
    {
        v_SQVM_RaiseError(v, "SetLightingOrigin requires a vector argument");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    DevMsg(eDLL_T::CLIENT, "SetLightingOrigin called - stub\n");
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//---------------------------------------------------------------------------------
// Purpose: script code class function registration
//---------------------------------------------------------------------------------
static void Script_RegisterClientEntityClassFuncs()
{
    v_Script_RegisterClientEntityClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;

    g_clientScriptEntityStruct->AddFunction(
        "HasModel",
        "Script_HasModel",
        "Returns true if entity has a model",
        "bool",
        "",
        false,
        Script_HasModel);

    g_clientScriptEntityStruct->AddFunction(
        "SetLightingOrigin",
        "Script_SetLightingOrigin",
        "Sets the lighting origin for entity rendering",
        "void",
        "vector lightingOrigin",
        false,
        Script_SetLightingOrigin);

    WeaponScriptVars_RegisterEntityFuncs(g_clientScriptEntityStruct);
}
//---------------------------------------------------------------------------------
static void Script_RegisterClientPlayerClassFuncs()
{
    v_Script_RegisterClientPlayerClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;

    Script_RegisterPlayerScriptFunctions(g_clientScriptPlayerStruct);
    WeaponScriptVars_RegisterPhaseShiftOverride(g_clientScriptPlayerStruct);
}
//---------------------------------------------------------------------------------
static void Script_RegisterClientAIClassFuncs()
{
    v_Script_RegisterClientAIClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterClientWeaponClassFuncs()
{
    v_Script_RegisterClientWeaponClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;

    ViewmodelPoseParam_RegisterClientWeaponFuncs(g_clientScriptWeaponStruct);
    WeaponScriptVars_RegisterWeaponFuncs(g_clientScriptWeaponStruct);
    WeaponScriptVars_RegisterInfiniteAmmoFuncs(g_clientScriptWeaponStruct);
    WeaponHeat_RegisterWeaponFuncs(g_clientScriptWeaponStruct);
}
//---------------------------------------------------------------------------------
static void Script_RegisterClientProjectileClassFuncs()
{
    v_Script_RegisterClientProjectileClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterClientTitanSoulClassFuncs()
{
    v_Script_RegisterClientTitanSoulClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterClientPlayerDecoyClassFuncs()
{
    v_Script_RegisterClientPlayerDecoyClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterClientFirstPersonProxyClassFuncs()
{
    v_Script_RegisterClientFirstPersonProxyClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}

void VScriptClient::Detour(const bool bAttach) const
{
    DetourSetup(&v_ClientScript_DebugScreenText, &ClientScript_DebugScreenText, bAttach);
    DetourSetup(&v_ClientScript_DebugScreenTextWithColor, &ClientScript_DebugScreenTextWithColor, bAttach);

    DetourSetup(&v_Script_RegisterClientEntityClassFuncs, &Script_RegisterClientEntityClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterClientPlayerClassFuncs, &Script_RegisterClientPlayerClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterClientAIClassFuncs, &Script_RegisterClientAIClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterClientWeaponClassFuncs, &Script_RegisterClientWeaponClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterClientProjectileClassFuncs, &Script_RegisterClientProjectileClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterClientTitanSoulClassFuncs, &Script_RegisterClientTitanSoulClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterClientPlayerDecoyClassFuncs, &Script_RegisterClientPlayerDecoyClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterClientFirstPersonProxyClassFuncs, &Script_RegisterClientFirstPersonProxyClassFuncs, bAttach);
}
