//=============================================================================//
// Purpose: RUI Wall Time script natives
//
// Registers RuiSetWallTimeWithOffset, RuiSetWallTimeBad, RuiSetWallTimeWithNow
// and their IfExists variants. Uses UIDT_WALLTIME (type 10) with uint64
// microsecond values instead of UIDT_GAMETIME (type 9) with float seconds.
//=============================================================================//

#include "core/stdafx.h"
#include "rui_walltime.h"
#include "tier0/memaddr.h"
#include "vscript/vscript.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "game/shared/vscript_gamedll_defs.h"
#include "vscript/languages/squirrel_re/vsquirrel.h"
#include "game/shared/vscript_shared.h"
#include "rtech/rui/rui.h"

typedef int (*GetRuiHandleFromScript_fn)(HSQUIRRELVM vm, int errorMode);

typedef void* (*UI_FindVarData_fn)(void* varDataCtx, const char* varName,
                                    uint8_t varType, uint8_t dataType);

static GetRuiHandleFromScript_fn s_GetRuiHandle = nullptr;
static UI_FindVarData_fn s_UI_FindVarData = nullptr;
static uintptr_t s_ruiClInstsBase = 0;
static uintptr_t s_iatFindVarData = 0;
static bool s_bInitialized = false;

static constexpr uint8_t UIDT_WALLTIME = 0x0A;
static constexpr uint8_t UIVAR_ARG = 0x00;
static constexpr size_t RUI_INST_STRIDE = 40;
static constexpr int RUI_INST_HANDLE_INVALID = -1;
static constexpr int RUI_INST_MASK = 0xFFF;

static uint64_t WallTime_ReadUS()
{
    static LARGE_INTEGER s_freq = {};
    if (!s_freq.QuadPart)
        QueryPerformanceFrequency(&s_freq);

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    return static_cast<uint64_t>(
        (static_cast<double>(now.QuadPart) * 1000000.0) /
        static_cast<double>(s_freq.QuadPart));
}

static SQRESULT RuiSetWallTimeInternal(HSQUIRRELVM v, uint64_t wallTimeValue,
                                        bool errorIfNotExists)
{
    if (!s_bInitialized)
    {
        if (errorIfNotExists)
            v_SQVM_ScriptError("RuiSetWallTime: not initialized");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    int handle = s_GetRuiHandle(v, 0);
    if (handle == RUI_INST_HANDLE_INVALID)
    {
        if (errorIfNotExists)
            v_SQVM_ScriptError("RuiSetWallTime: invalid RUI handle");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    int instIdx = handle & RUI_INST_MASK;
    uintptr_t instPtr = s_ruiClInstsBase + (instIdx * RUI_INST_STRIDE);

    uintptr_t uiInst = *reinterpret_cast<uintptr_t*>(instPtr + 8);
    if (!uiInst)
    {
        if (errorIfNotExists)
            v_SQVM_ScriptError("RuiSetWallTime: null uiInst");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    struct VarDataCtx {
        uintptr_t uiInst;
        uint64_t arrayData;
    } ctx;
    ctx.uiInst = uiInst;
    ctx.arrayData = 0;

    const SQChar* argName = nullptr;
    if (SQ_FAILED(sq_getstring(v, 3, &argName)) || !argName)
    {
        if (errorIfNotExists)
            v_SQVM_ScriptError("RuiSetWallTime: invalid arg name");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    void* varData = s_UI_FindVarData(&ctx, argName, UIVAR_ARG, UIDT_WALLTIME);
    if (!varData)
    {
        if (errorIfNotExists)
        {
            const char* assetName = "unknown";
            uintptr_t uiAsset = *reinterpret_cast<uintptr_t*>(uiInst);
            if (uiAsset)
            {
                const char* name = *reinterpret_cast<const char**>(uiAsset);
                if (name)
                    assetName = name;
            }
            v_SQVM_ScriptError("UI %s doesn't expose argument %s as a wall time",
                assetName, argName);
        }
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    *reinterpret_cast<uint64_t*>(varData) = wallTimeValue;

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT ClientScript_RuiSetWallTimeWithOffset(HSQUIRRELVM v)
{
    SQFloat offset = 0.0f;
    if (SQ_FAILED(sq_getfloat(v, 4, &offset)))
    {
        v_SQVM_ScriptError("RuiSetWallTimeWithOffset: expected float offset");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    if (offset != offset)
    {
        v_SQVM_ScriptError("Tried to set a NAN.");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    uint64_t nowUS = WallTime_ReadUS();
    double offsetUS = static_cast<double>(offset) * 1000000.0;

    uint64_t wallTime;
    if (offsetUS >= 0.0)
        wallTime = nowUS + static_cast<uint64_t>(offsetUS);
    else
    {
        uint64_t absOffset = static_cast<uint64_t>(-offsetUS);
        wallTime = (nowUS > absOffset) ? (nowUS - absOffset) : 0;
    }

    return RuiSetWallTimeInternal(v, wallTime, true);
}

static SQRESULT ClientScript_RuiSetWallTimeWithNow(HSQUIRRELVM v)
{
    return RuiSetWallTimeInternal(v, WallTime_ReadUS(), true);
}

static SQRESULT ClientScript_RuiSetWallTimeBad(HSQUIRRELVM v)
{
    return RuiSetWallTimeInternal(v, 0, true);
}

static SQRESULT ClientScript_RuiSetWallTimeWithOffsetIfExists(HSQUIRRELVM v)
{
    SQFloat offset = 0.0f;
    if (SQ_FAILED(sq_getfloat(v, 4, &offset)))
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

    if (offset != offset)
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

    uint64_t nowUS = WallTime_ReadUS();
    double offsetUS = static_cast<double>(offset) * 1000000.0;

    uint64_t wallTime;
    if (offsetUS >= 0.0)
        wallTime = nowUS + static_cast<uint64_t>(offsetUS);
    else
    {
        uint64_t absOffset = static_cast<uint64_t>(-offsetUS);
        wallTime = (nowUS > absOffset) ? (nowUS - absOffset) : 0;
    }

    return RuiSetWallTimeInternal(v, wallTime, false);
}

static SQRESULT ClientScript_RuiSetWallTimeWithNowIfExists(HSQUIRRELVM v)
{
    return RuiSetWallTimeInternal(v, WallTime_ReadUS(), false);
}

static SQRESULT ClientScript_RuiSetWallTimeBadIfExists(HSQUIRRELVM v)
{
    return RuiSetWallTimeInternal(v, 0, false);
}

void RuiWallTime_RegisterNatives(CSquirrelVM* s)
{
    if (!s_bInitialized)
    {
        Warning(eDLL_T::CLIENT, "RuiWallTime: cannot register natives - not initialized\n");
        return;
    }

    // IAT isn't populated at Detour() time; resolve on first registration
    if (!s_UI_FindVarData && s_iatFindVarData)
    {
        s_UI_FindVarData = *reinterpret_cast<UI_FindVarData_fn*>(s_iatFindVarData);
        if (s_UI_FindVarData)
            Msg(eDLL_T::CLIENT, "RuiWallTime: UI_FindVarData at %p\n", s_UI_FindVarData);
        else
        {
            Warning(eDLL_T::CLIENT, "RuiWallTime: UI_FindVarData still NULL at registration\n");
            return;
        }
    }

    Script_RegisterFuncNamed(s, "RuiSetWallTimeWithOffset",
        "Client_Script_RuiSetWallTimeWithOffset",
        "Sets a walltime argument with the current time plus an offset in seconds",
        "void", "var, string, float", false,
        ClientScript_RuiSetWallTimeWithOffset);

    Script_RegisterFuncNamed(s, "RuiSetWallTimeWithNow",
        "Client_Script_RuiSetWallTimeWithNow",
        "Sets a walltime argument to the current time",
        "void", "var, string", false,
        ClientScript_RuiSetWallTimeWithNow);

    Script_RegisterFuncNamed(s, "RuiSetWallTimeBad",
        "Client_Script_RuiSetWallTimeBad",
        "Resets a walltime argument to badWallTime",
        "void", "var, string", false,
        ClientScript_RuiSetWallTimeBad);

    Script_RegisterFuncNamed(s, "RuiSetWallTimeWithOffsetIfExists",
        "Client_Script_RuiSetWallTimeWithOffsetIfExists",
        "Sets a walltime argument if it exists on the RUI",
        "void", "var, string, float", false,
        ClientScript_RuiSetWallTimeWithOffsetIfExists);

    Script_RegisterFuncNamed(s, "RuiSetWallTimeWithNowIfExists",
        "Client_Script_RuiSetWallTimeWithNowIfExists",
        "Sets a walltime argument to the current time if it exists",
        "void", "var, string", false,
        ClientScript_RuiSetWallTimeWithNowIfExists);

    Script_RegisterFuncNamed(s, "RuiSetWallTimeBadIfExists",
        "Client_Script_RuiSetWallTimeBadIfExists",
        "Resets a walltime argument to badWallTime if it exists",
        "void", "var, string", false,
        ClientScript_RuiSetWallTimeBadIfExists);

    s->RegisterConstant("RUI_BADWALLTIME", 0);
}

void RuiWallTime_Init()
{
    if (s_bInitialized) return;

    CMemory fnSetGameTime = Module_FindPattern(g_GameDll,
        "40 56 57 48 83 EC 48 48 89 5C 24 ? 33 D2 48 89 6C 24 ? 48 8B F1");

    if (!fnSetGameTime)
    {
        Error(eDLL_T::CLIENT, NO_ERROR,
            "RuiWallTime: failed to find Script_Rui_SetGameTime\n");
        return;
    }

    Msg(eDLL_T::CLIENT, "RuiWallTime: Script_Rui_SetGameTime at %p\n",
        fnSetGameTime.GetPtr());

    // GetRuiHandleFromScript: E8 relative call at +0x20
    CMemory callGetHandle = fnSetGameTime.Offset(0x20);
    s_GetRuiHandle = callGetHandle.FollowNearCallSelf()
        .RCast<GetRuiHandleFromScript_fn>();

    if (!s_GetRuiHandle)
    {
        Error(eDLL_T::CLIENT, NO_ERROR,
            "RuiWallTime: failed to resolve GetRuiHandleFromScript\n");
        return;
    }
    Msg(eDLL_T::CLIENT, "RuiWallTime: GetRuiHandleFromScript at %p\n",
        s_GetRuiHandle);

    // s_ruiCl.insts: LEA rcx,[rip+disp32] at +0x2F
    CMemory leaInstsBase = fnSetGameTime.Offset(0x2F);
    s_ruiClInstsBase = leaInstsBase.ResolveRelativeAddress(0x3, 0x7)
        .GetPtr();

    if (!s_ruiClInstsBase)
    {
        Error(eDLL_T::CLIENT, NO_ERROR,
            "RuiWallTime: failed to resolve s_ruiCl.insts base\n");
        return;
    }
    Msg(eDLL_T::CLIENT, "RuiWallTime: s_ruiCl.insts at 0x%llX\n",
        s_ruiClInstsBase);

    // UI_FindVarData: FF 15 indirect call at +0x6A (IAT, resolved lazily)
    CMemory callFindVar = fnSetGameTime.Offset(0x6A);
    s_iatFindVarData = callFindVar.ResolveRelativeAddress(0x2, 0x6)
        .GetPtr();

    if (!s_iatFindVarData)
    {
        Error(eDLL_T::CLIENT, NO_ERROR,
            "RuiWallTime: failed to resolve UI_FindVarData IAT address\n");
        return;
    }
    Msg(eDLL_T::CLIENT, "RuiWallTime: UI_FindVarData IAT at 0x%llX\n",
        s_iatFindVarData);

    s_bInitialized = true;
    Msg(eDLL_T::CLIENT, "RuiWallTime: initialized successfully\n");
}

void VRuiWallTime::GetAdr(void) const
{
    LogFunAdr("GetRuiHandleFromScript", s_GetRuiHandle);
    LogFunAdr("UI_FindVarData", s_UI_FindVarData);
    LogVarAdr("s_ruiCl.insts", reinterpret_cast<void*>(s_ruiClInstsBase));
}

void VRuiWallTime::GetFun(void) const { }
void VRuiWallTime::GetVar(void) const { }
void VRuiWallTime::GetCon(void) const { }

void VRuiWallTime::Detour(const bool bAttach) const
{
    RuiWallTime_Init();
}
