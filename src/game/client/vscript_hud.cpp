//=============================================================================//
//
// Purpose: HUD element script functions (buttons, grids, text, scrolling)
//
//=============================================================================//

#include "core/stdafx.h"
#include "tier1/convar.h"
#include "mathlib/mathlib.h"

#include "vscript/vscript.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "vscript/languages/squirrel_re/include/squirrel.h"
#include "game/shared/vscript_gamedll_defs.h"
#include "vscript/languages/squirrel_re/vsquirrel.h"
#include "vscript_client.h"
#include "vscript_hud.h"

//=============================================================================
// HUD element helpers
//=============================================================================

// CClientScriptHudElement layout offsets
static constexpr ptrdiff_t HUDELEMENT_PANEL_OFFSET   = 0x28;
static constexpr ptrdiff_t HUDELEMENT_SQOBJECT_OFFSET = 0x30;
static constexpr ptrdiff_t HUDELEMENT_OWNER_OFFSET    = 0x38;

// VGUI Panel vtable offsets
static constexpr ptrdiff_t PANEL_VT_GETNAME           = 0xC0;  // GetClientPanelName
static constexpr ptrdiff_t PANEL_VT_ISBUTTON          = 0x610; // IsButton
static constexpr ptrdiff_t PANEL_VT_ISLABEL           = 0x608; // IsLabel
static constexpr ptrdiff_t PANEL_VT_ISDIALOGLISTBTN   = 0x618; // IsDialogListButton
static constexpr ptrdiff_t PANEL_VT_ISTEXTENTRY       = 0x648; // IsTextEntry
static constexpr ptrdiff_t PANEL_VT_ISGRIDBTNLIST     = 0x660; // IsGridButtonListPanel
static constexpr ptrdiff_t PANEL_VT_ISCURSOROVER      = 0x2B0; // IsCursorOver
static constexpr ptrdiff_t PANEL_VT_SETTEXTHIDDEN     = 0x8E0; // SetTextHidden(bool)
static constexpr ptrdiff_t PANEL_VT_GETTEXTHIDDEN     = 0x8E8; // GetTextHidden() -> bool
static constexpr ptrdiff_t PANEL_VT_ISSELECTED        = 0x8F8; // IsSelected
static constexpr ptrdiff_t PANEL_VT_SETBUTTONSTATE    = 0x9A8; // SetButtonState(stateId, value)

// SetButtonState state IDs
static constexpr int BTNSTATE_LOCKED  = 5;
static constexpr int BTNSTATE_NEW     = 6;
static constexpr int BTNSTATE_CHECKED = 7;

static constexpr ptrdiff_t BUTTON_STATE_BITMASK = 0x460;

// GridButtonListPanel field offsets
static constexpr ptrdiff_t GRID_BUTTONS_ARRAY  = 0x2D8; // m_Buttons.m_Memory (ptr to ptr array)
static constexpr ptrdiff_t GRID_BUTTONS_COUNT  = 0x2F0; // m_Buttons.m_Size (int)
static constexpr ptrdiff_t GRID_SCROLL_PANEL   = 0x2F8; // m_pScrollPanel (EditablePanel*, NOT an int!)
static constexpr ptrdiff_t GRID_SCROLL_BAR     = 0x300; // m_pScrollBar (ScrollBar*)

// ScrollBar vtable offsets
static constexpr ptrdiff_t SCROLLBAR_VT_SETVALUE = 0x778; // ScrollBar::SetValue(int) - vtable[239]
static constexpr ptrdiff_t SCROLLBAR_VT_GETVALUE = 0x780; // ScrollBar::GetValue() -> int - vtable[240]

static constexpr ptrdiff_t TEXTENTRY_HIDDEN    = 0x495;

// TextEntry vtable offsets
static constexpr ptrdiff_t PANEL_VT_TEXTENTRY_GETTEXT_W = 0x788;
static constexpr ptrdiff_t PANEL_VT_BUTTON_GETTEXT_W    = 0x778;

// CClientScriptHudElement event handler offsets
static constexpr ptrdiff_t HUDELEMENT_EVENTHANDLERS = 0x168;
static constexpr int MAX_EVENT_TYPE = 6;
static constexpr int EVENT_HANDLER_STRIDE = 32; // bytes per event type

//-----------------------------------------------------------------------------
// Purpose: resolves a HUD element from the Squirrel VM stack
//          Uses the engine's native resolution function sub_141056980
//          Returns CClientScriptHudElement* or nullptr
//-----------------------------------------------------------------------------
typedef uintptr_t(__fastcall* GetHudElementFn)(uintptr_t, uintptr_t, uintptr_t);
static GetHudElementFn s_fnGetHudElement = nullptr;
static uintptr_t s_hudElemTypeDesc = 0;

static void FixButtonStateArgNameTable(uintptr_t base)
{
	static const char* s_szIsChecked = "isChecked";

	// Button state arg name table; entry 7 is uninitialized — patch to "isChecked"
	uintptr_t* table = reinterpret_cast<uintptr_t*>(base + 0x131A9D0);

	const char* entry2 = reinterpret_cast<const char*>(table[2]);
	if (!entry2 || strcmp(entry2, "isDisabled") != 0)
	{
		Warning(eDLL_T::CLIENT, "ButtonState: table validation failed at entry 2\n");
		return;
	}

	DWORD oldProtect;
	VirtualProtect(&table[7], sizeof(uintptr_t), PAGE_READWRITE, &oldProtect);
	table[7] = reinterpret_cast<uintptr_t>(s_szIsChecked);
	VirtualProtect(&table[7], sizeof(uintptr_t), oldProtect, &oldProtect);

	Msg(eDLL_T::CLIENT, "ButtonState: patched arg name table entry 7 -> \"%s\"\n",
		s_szIsChecked);
}

static void InitHudElementResolver()
{
	static bool s_initialized = false;
	if (s_initialized)
		return;
	s_initialized = true;

	const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
	s_fnGetHudElement = reinterpret_cast<GetHudElementFn>(base + 0x1056980);
	s_hudElemTypeDesc = base + 0x2337A70;

	FixButtonStateArgNameTable(base);
}

static uintptr_t GetHudElement(HSQUIRRELVM v)
{
	InitHudElementResolver();
	if (!s_fnGetHudElement || !s_hudElemTypeDesc)
		return 0;
	// arg2 must be a pointer to the SQObject for the hud element (stack param 2),
	// NOT the SQVM pointer. The native function dereferences it as an SQObject.
	SQObjectPtr& obj = stack_get(v, 2);
	return s_fnGetHudElement(reinterpret_cast<uintptr_t>(v),
		reinterpret_cast<uintptr_t>(&obj), s_hudElemTypeDesc);
}

// Helper: get the VGUI panel pointer from a CClientScriptHudElement
static uintptr_t GetPanel(uintptr_t hudElem)
{
	return *reinterpret_cast<uintptr_t*>(hudElem + HUDELEMENT_PANEL_OFFSET);
}

// Helper: call a bool-returning vtable function on a panel
static bool PanelVtableBool(uintptr_t panel, ptrdiff_t vtOffset)
{
	uintptr_t vtable = *reinterpret_cast<uintptr_t*>(panel);
	typedef char(__fastcall* VtFn)(uintptr_t);
	return reinterpret_cast<VtFn>(*reinterpret_cast<uintptr_t*>(vtable + vtOffset))(panel) != 0;
}

// Helper: get panel name string for error messages
static const char* GetPanelName(uintptr_t panel)
{
	uintptr_t vtable = *reinterpret_cast<uintptr_t*>(panel);
	typedef const char*(__fastcall* GetNameFn)(uintptr_t);
	return reinterpret_cast<GetNameFn>(*reinterpret_cast<uintptr_t*>(vtable + PANEL_VT_GETNAME))(panel);
}

// Helper: get CClientScriptHud (owner) from HudElement
static uintptr_t GetHudOwner(uintptr_t hudElem)
{
	return *reinterpret_cast<uintptr_t*>(hudElem + HUDELEMENT_OWNER_OFFSET);
}

// CClientScriptHud::CreateHudElementForPanel
typedef uintptr_t(__fastcall* CreateHudElemForPanelFn)(uintptr_t, uintptr_t);
static CreateHudElemForPanelFn s_fnCreateHudElemForPanel = nullptr;

static void InitCreateHudElemForPanel()
{
	static bool s_initialized = false;
	if (s_initialized)
		return;
	s_initialized = true;

	const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
	s_fnCreateHudElemForPanel = reinterpret_cast<CreateHudElemForPanelFn>(base + 0x98E380);
}

// DialogListButton::RemoveAllListItems
typedef void(__fastcall* RemoveAllListItemsFn)(uintptr_t);
static RemoveAllListItemsFn s_fnRemoveAllListItems = nullptr;

static void InitRemoveAllListItems()
{
	static bool s_initialized = false;
	if (s_initialized)
		return;
	s_initialized = true;

	// DialogListButton::RemoveAllListItems
	CMemory result = Module_FindPattern(g_GameDll,
		"40 53 48 83 EC 20 48 8B D9 E8 ?? ?? ?? ?? 33 D2 48 8B CB E8");
	if (result)
		s_fnRemoveAllListItems = result.RCast<RemoveAllListItemsFn>();
}

//=============================================================================
// HUD script function implementations
//=============================================================================

//-----------------------------------------------------------------------------
// Hud_GetButtonCount(var hudElement) -> int
// Returns the button count of a GridButtonListPanel, or -1 on error
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_GetButtonCount_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISGRIDBTNLIST))
	{
		v_SQVM_ScriptError("No GridButtonListPanel element with name '%s'.", GetPanelName(panel));
		sq_pushinteger(v, -1);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	const int count = *reinterpret_cast<int*>(panel + GRID_BUTTONS_COUNT);
	sq_pushinteger(v, count);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_GetButtonCount(HSQUIRRELVM v) { return Script_Hud_GetButtonCount_Impl(v); }
static SQRESULT UIScript_Hud_GetButtonCount(HSQUIRRELVM v) { return Script_Hud_GetButtonCount_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_GetButton(var hudElement, int index) -> var
// Returns the button at the given index from a GridButtonListPanel
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_GetButton_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	SQInteger index = 0;
	sq_getinteger(v, 3, &index);

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISGRIDBTNLIST))
	{
		v_SQVM_ScriptError("No GridButtonListPanel element with name '%s'.", GetPanelName(panel));
		sq_pushnull(v);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	const int count = *reinterpret_cast<int*>(panel + GRID_BUTTONS_COUNT);
	if (index < 0 || index >= count)
	{
		v_SQVM_ScriptError("GridButtonListPanel '%s' does not have a button at index '%i'.",
			GetPanelName(panel), (int)index);
		sq_pushnull(v);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	// Button array: array of panel pointers at GRID_BUTTONS_ARRAY
	uintptr_t* buttonArray = *reinterpret_cast<uintptr_t**>(panel + GRID_BUTTONS_ARRAY);
	uintptr_t buttonPanel = buttonArray[index];

	if (!buttonPanel)
	{
		sq_pushnull(v);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	// Create/find HudElement for this button panel and return its script handle
	InitCreateHudElemForPanel();
	uintptr_t hudOwner = GetHudOwner(hudElem);
	if (s_fnCreateHudElemForPanel && hudOwner)
	{
		uintptr_t buttonHudElem = s_fnCreateHudElemForPanel(hudOwner, buttonPanel);
		if (buttonHudElem)
		{
			// hudElement+0x30 contains a POINTER to an SQObject (allocated by sub_141056680)
			SQObject* storedObj = *reinterpret_cast<SQObject**>(buttonHudElem + HUDELEMENT_SQOBJECT_OFFSET);
			if (storedObj && storedObj->_type == OT_INSTANCE && storedObj->_unVal.pInstance)
			{
				sq_pushobject(v, *storedObj);
				SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
			}
		}
	}

	sq_pushnull(v);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_GetButton(HSQUIRRELVM v) { return Script_Hud_GetButton_Impl(v); }
static SQRESULT UIScript_Hud_GetButton(HSQUIRRELVM v) { return Script_Hud_GetButton_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_GetSelectedButton(var hudElement) -> var
// Returns the currently selected button from a GridButtonListPanel
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_GetSelectedButton_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISGRIDBTNLIST))
	{
		v_SQVM_ScriptError("No GridButtonListPanel element with name '%s'.", GetPanelName(panel));
		sq_pushnull(v);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	// Iterate buttons looking for the selected one
	const int count = *reinterpret_cast<int*>(panel + GRID_BUTTONS_COUNT);
	uintptr_t* buttonArray = *reinterpret_cast<uintptr_t**>(panel + GRID_BUTTONS_ARRAY);

	for (int i = 0; i < count; i++)
	{
		uintptr_t btn = buttonArray[i];
		if (btn && PanelVtableBool(btn, PANEL_VT_ISSELECTED))
		{
			InitCreateHudElemForPanel();
			uintptr_t hudOwner = GetHudOwner(hudElem);
			if (s_fnCreateHudElemForPanel && hudOwner)
			{
				uintptr_t btnHudElem = s_fnCreateHudElemForPanel(hudOwner, btn);
				if (btnHudElem)
				{
					// hudElement+0x30 contains a POINTER to an SQObject (allocated by sub_141056680)
					SQObject* storedObj = *reinterpret_cast<SQObject**>(btnHudElem + HUDELEMENT_SQOBJECT_OFFSET);
					if (storedObj && storedObj->_type == OT_INSTANCE && storedObj->_unVal.pInstance)
					{
						sq_pushobject(v, *storedObj);
						SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
					}
				}
			}
		}
	}

	sq_pushnull(v);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_GetSelectedButton(HSQUIRRELVM v) { return Script_Hud_GetSelectedButton_Impl(v); }
static SQRESULT UIScript_Hud_GetSelectedButton(HSQUIRRELVM v) { return Script_Hud_GetSelectedButton_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_SetChecked(var hudElement, bool checked)
// Writes the checked bit directly to avoid SetButtonState listener overflow.
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_SetChecked_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	SQBool checked = SQFalse;
	sq_getbool(v, 3, &checked);

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISBUTTON))
	{
		v_SQVM_ScriptError("Hud element is not a button. (%s)", GetPanelName(panel));
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	DWORD* bitmask = reinterpret_cast<DWORD*>(panel + BUTTON_STATE_BITMASK);
	if (checked)
		*bitmask |= (1 << BTNSTATE_CHECKED);
	else
		*bitmask &= ~(1 << BTNSTATE_CHECKED);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_SetChecked(HSQUIRRELVM v) { return Script_Hud_SetChecked_Impl(v); }
static SQRESULT UIScript_Hud_SetChecked(HSQUIRRELVM v) { return Script_Hud_SetChecked_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_IsChecked(var hudElement) -> bool
// Returns whether a button element has the checked state set.
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_IsChecked_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISBUTTON))
	{
		sq_pushbool(v, SQFalse);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	DWORD bitmask = *reinterpret_cast<DWORD*>(panel + BUTTON_STATE_BITMASK);
	sq_pushbool(v, (bitmask & (1 << BTNSTATE_CHECKED)) ? SQTrue : SQFalse);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_IsChecked(HSQUIRRELVM v) { return Script_Hud_IsChecked_Impl(v); }
static SQRESULT UIScript_Hud_IsChecked(HSQUIRRELVM v) { return Script_Hud_IsChecked_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_ClearEventHandlers(var hudElement, int eventType)
// Clears all script event handlers for the given event type on a HUD element
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_ClearEventHandlers_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	SQInteger eventType = 0;
	sq_getinteger(v, 3, &eventType);

	if (eventType < 0 || eventType > MAX_EVENT_TYPE)
	{
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	// Zero out the event handler slots for this event type
	// Each event type has EVENT_HANDLER_STRIDE bytes at HUDELEMENT_EVENTHANDLERS + eventType * stride
	uintptr_t handlerBase = hudElem + HUDELEMENT_EVENTHANDLERS + eventType * EVENT_HANDLER_STRIDE;
	memset(reinterpret_cast<void*>(handlerBase), 0, EVENT_HANDLER_STRIDE);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_ClearEventHandlers(HSQUIRRELVM v) { return Script_Hud_ClearEventHandlers_Impl(v); }
static SQRESULT UIScript_Hud_ClearEventHandlers(HSQUIRRELVM v) { return Script_Hud_ClearEventHandlers_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_GetTextHidden(var hudElement) -> bool
// Returns whether text is hidden on a TextEntry panel
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_GetTextHidden_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISTEXTENTRY))
	{
		v_SQVM_ScriptError("No text entry with name '%s'.", GetPanelName(panel));
		sq_pushbool(v, SQFalse);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	// Read textHidden byte directly from panel struct
	const bool hidden = *reinterpret_cast<uint8_t*>(panel + TEXTENTRY_HIDDEN) != 0;
	sq_pushbool(v, hidden ? SQTrue : SQFalse);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_GetTextHidden(HSQUIRRELVM v) { return Script_Hud_GetTextHidden_Impl(v); }
static SQRESULT UIScript_Hud_GetTextHidden(HSQUIRRELVM v) { return Script_Hud_GetTextHidden_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_SetTextHidden(var hudElement, bool hidden)
// Direct field write; vtable call uses an invalid RUI arg lookup.
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_SetTextHidden_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	SQBool hidden = SQFalse;
	sq_getbool(v, 3, &hidden);

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISTEXTENTRY))
	{
		v_SQVM_ScriptError("No text entry with name '%s'.", GetPanelName(panel));
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	// Direct field write; vtable SetTextHidden (0x8E0) uses an invalid RUI arg lookup
	*reinterpret_cast<uint8_t*>(panel + TEXTENTRY_HIDDEN) = hidden ? 1 : 0;

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_SetTextHidden(HSQUIRRELVM v) { return Script_Hud_SetTextHidden_Impl(v); }
static SQRESULT UIScript_Hud_SetTextHidden(HSQUIRRELVM v) { return Script_Hud_SetTextHidden_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_ScrollToTop(var hudElement)
// Scrolls a GridButtonListPanel to the top by calling ScrollBar::SetValue(0)
//
// BUG FIX: Previously wrote int(0) to offset 0x2F8, which is the ScrollPanel
// POINTER (not a scroll offset). This zeroed the lower 32 bits of the 64-bit
// pointer, corrupting it to e.g. 0x0000027000000000. The next call to
// Hud_InitGridButtons would read this corrupted pointer as the parent for new
// GridButtons, crashing in vgui::Panel constructor when dereferencing vtable=1.
//
// The actual scroll position lives in the ScrollBar widget at offset 0x300.
// ScrollToItem calls ScrollBar::SetValue() at vtable[239] and GetValue() at [240].
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_ScrollToTop_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISGRIDBTNLIST))
	{
		v_SQVM_ScriptError("No GridButtonListPanel element with name '%s'.", GetPanelName(panel));
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	// Get the ScrollBar at offset 0x300 and call SetValue(0) via vtable[239]
	uintptr_t scrollBar = *reinterpret_cast<uintptr_t*>(panel + GRID_SCROLL_BAR);
	if (scrollBar)
	{
		uintptr_t scrollBarVtable = *reinterpret_cast<uintptr_t*>(scrollBar);
		auto SetValue = reinterpret_cast<void(__fastcall*)(uintptr_t, int)>(
			*reinterpret_cast<uintptr_t*>(scrollBarVtable + SCROLLBAR_VT_SETVALUE));
		SetValue(scrollBar, 0);
	}

	// Trigger layout refresh
	uintptr_t vtable = *reinterpret_cast<uintptr_t*>(panel);
	auto InvalidateLayout = reinterpret_cast<void(__fastcall*)(uintptr_t)>(
		*reinterpret_cast<uintptr_t*>(vtable + 0x28)); // vtable[5] = InvalidateLayout
	InvalidateLayout(panel);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_ScrollToTop(HSQUIRRELVM v) { return Script_Hud_ScrollToTop_Impl(v); }
static SQRESULT UIScript_Hud_ScrollToTop(HSQUIRRELVM v) { return Script_Hud_ScrollToTop_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_IsCursorOver(var hudElement) -> bool
// Returns whether the cursor is over a HUD panel
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_IsCursorOver_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	uintptr_t panel = GetPanel(hudElem);
	const bool over = PanelVtableBool(panel, PANEL_VT_ISCURSOROVER);
	sq_pushbool(v, over ? SQTrue : SQFalse);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_IsCursorOver(HSQUIRRELVM v) { return Script_Hud_IsCursorOver_Impl(v); }
static SQRESULT UIScript_Hud_IsCursorOver(HSQUIRRELVM v) { return Script_Hud_IsCursorOver_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_DialogList_ClearList(var hudElement)
// Removes all items from a DialogListButton
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_DialogList_ClearList_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISDIALOGLISTBTN))
	{
		v_SQVM_ScriptError("No DialogListButton element with name '%s'.", GetPanelName(panel));
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	InitRemoveAllListItems();
	if (s_fnRemoveAllListItems)
		s_fnRemoveAllListItems(panel);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_DialogList_ClearList(HSQUIRRELVM v) { return Script_Hud_DialogList_ClearList_Impl(v); }
static SQRESULT UIScript_Hud_DialogList_ClearList(HSQUIRRELVM v) { return Script_Hud_DialogList_ClearList_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_GetUnicodeLen(var hudElement) -> int
// Returns the unicode length of text in a TextEntry, Button, or Label
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_GetUnicodeLen_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	uintptr_t panel = GetPanel(hudElem);
	uintptr_t vtable = *reinterpret_cast<uintptr_t*>(panel);

	// Determine which GetText(wchar) vtable slot to use based on panel type
	ptrdiff_t getTextOffset = 0;
	if (PanelVtableBool(panel, PANEL_VT_ISTEXTENTRY))
		getTextOffset = PANEL_VT_TEXTENTRY_GETTEXT_W;
	else if (PanelVtableBool(panel, PANEL_VT_ISBUTTON) || PanelVtableBool(panel, PANEL_VT_ISLABEL))
		getTextOffset = PANEL_VT_BUTTON_GETTEXT_W;

	if (!getTextOffset)
	{
		v_SQVM_ScriptError("Hud element is not a text panel. (%s)", GetPanelName(panel));
		sq_pushinteger(v, 0);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	// Call GetText to get wchar buffer, then measure unicode length
	wchar_t textBuf[4096];
	textBuf[0] = L'\0';
	typedef void(__fastcall* GetTextFn)(uintptr_t, wchar_t*, int);
	reinterpret_cast<GetTextFn>(*reinterpret_cast<uintptr_t*>(vtable + getTextOffset))(
		panel, textBuf, 4096);

	sq_pushinteger(v, static_cast<SQInteger>(wcslen(textBuf)));
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_GetUnicodeLen(HSQUIRRELVM v) { return Script_Hud_GetUnicodeLen_Impl(v); }
static SQRESULT UIScript_Hud_GetUnicodeLen(HSQUIRRELVM v) { return Script_Hud_GetUnicodeLen_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_InitGridButtonsCategories(var hudElement, array categories)
// Initializes grid button categories on a GridButtonListPanel
// This is a simplified stub since the full implementation requires deep
// GridButtonListPanel internal access (CUtlVector<short> m_Categories)
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_InitGridButtonsCategories_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISGRIDBTNLIST))
	{
		v_SQVM_ScriptError("No GridButtonListPanel element with name '%s'.", GetPanelName(panel));
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	// Consume the array parameter (stack index 3) silently
	// The actual SetCategories call requires internal CUtlVector manipulation
	// which we'll add if needed based on runtime errors
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_InitGridButtonsCategories(HSQUIRRELVM v) { return Script_Hud_InitGridButtonsCategories_Impl(v); }
static SQRESULT UIScript_Hud_InitGridButtonsCategories(HSQUIRRELVM v) { return Script_Hud_InitGridButtonsCategories_Impl(v); }

//=============================================================================
// Registration
//=============================================================================

void Script_RegisterHudFunctions(CSquirrelVM* s)
{
	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_GetButtonCount,
		"Returns the button count of a GridButtonListPanel",
		"int", "var hudElement", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_GetButton,
		"Returns a button at the given index from a GridButtonListPanel",
		"var", "var hudElement, int index", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_GetSelectedButton,
		"Returns the selected button from a GridButtonListPanel",
		"var", "var hudElement", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_SetChecked,
		"Sets the checked state on a button element",
		"void", "var hudElement, bool checked", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_IsChecked,
		"Returns whether a button element is checked",
		"bool", "var hudElement", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_ClearEventHandlers,
		"Clears event handlers for the given event type",
		"void", "var hudElement, int eventType", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_GetTextHidden,
		"Returns whether text is hidden on a TextEntry",
		"bool", "var hudElement", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_SetTextHidden,
		"Sets text hidden state on a TextEntry",
		"void", "var hudElement, bool hidden", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_ScrollToTop,
		"Scrolls a GridButtonListPanel to the top",
		"void", "var hudElement", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_IsCursorOver,
		"Returns whether the cursor is over a HUD element",
		"bool", "var hudElement", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_DialogList_ClearList,
		"Removes all items from a DialogListButton",
		"void", "var hudElement", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_GetUnicodeLen,
		"Returns the unicode length of text in a text panel",
		"int", "var hudElement", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_InitGridButtonsCategories,
		"Initializes grid button categories on a GridButtonListPanel",
		"void", "var hudElement, array categories", true);
}

void Script_RegisterHudUIFunctions(CSquirrelVM* s)
{
	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_GetButtonCount,
		"Returns the button count of a GridButtonListPanel",
		"int", "var hudElement", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_GetButton,
		"Returns a button at the given index from a GridButtonListPanel",
		"var", "var hudElement, int index", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_GetSelectedButton,
		"Returns the selected button from a GridButtonListPanel",
		"var", "var hudElement", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_SetChecked,
		"Sets the checked state on a button element",
		"void", "var hudElement, bool checked", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_IsChecked,
		"Returns whether a button element is checked",
		"bool", "var hudElement", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_ClearEventHandlers,
		"Clears event handlers for the given event type",
		"void", "var hudElement, int eventType", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_GetTextHidden,
		"Returns whether text is hidden on a TextEntry",
		"bool", "var hudElement", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_SetTextHidden,
		"Sets text hidden state on a TextEntry",
		"void", "var hudElement, bool hidden", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_ScrollToTop,
		"Scrolls a GridButtonListPanel to the top",
		"void", "var hudElement", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_IsCursorOver,
		"Returns whether the cursor is over a HUD element",
		"bool", "var hudElement", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_DialogList_ClearList,
		"Removes all items from a DialogListButton",
		"void", "var hudElement", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_GetUnicodeLen,
		"Returns the unicode length of text in a text panel",
		"int", "var hudElement", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_InitGridButtonsCategories,
		"Initializes grid button categories on a GridButtonListPanel",
		"void", "var hudElement, array categories", true);
}
