//=============================================================================//
//
// Purpose: Expose native code to VScript API
// 
//-----------------------------------------------------------------------------
// 
// The scripting API is designed using a Foreign Function Interface (FFI) pattern,
// allowing you to define functions in code and abstract them for use in target VM.
// 
// This allows computationally heavy tasks to be offloaded to native code while the
// scripts handle the less demanding operations. This also allows for interfacing
// script code directly with the engine and SDK code with minimal runtime overhead.
// 
// There are some rules for creating these abstractions:
// - all bindings must return using SCRIPT_CHECK_AND_RETURN().
// - all bindings must be declared as a static function.
// - all bindings take a single parameter, using the type HSQUIRRELVM.
// - all bindings must have the type SQRESULT as return value.
// - registration is done using DEFINE_<context>_SCRIPTFUNC_NAMED().
// 
// To create shared script bindings:
// - use the DEFINE_SHARED_SCRIPTFUNC_NAMED() macro.
// - prefix your function with "SharedScript_" i.e. "SharedScript_GetVersion".
// 
//=============================================================================//

#include "core/stdafx.h"
#include "rtech/playlists/playlists.h"
#include "rtech/datatable/datatable.h"
#include "engine/client/cl_main.h"
#include "engine/cmodel_bsp.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "vscript_gamedll_defs.h"
#include "vscript_shared.h"
#include "pluginsystem/pluginsystem.h"
#include "game/shared/pluginsystem/modsystem.h"
#include "vstdlib/random.h"

//-----------------------------------------------------------------------------
// Purpose: expose SDK version to the VScript API
//-----------------------------------------------------------------------------
static SQRESULT SharedScript_GetSDKVersion(HSQUIRRELVM v)
{
    sq_pushstring(v, SDK_VERSION, sizeof(SDK_VERSION) - 1);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: return all available maps
//-----------------------------------------------------------------------------
static SQRESULT SharedScript_GetAvailableMaps(HSQUIRRELVM v)
{
    AUTO_LOCK(g_InstalledMapsMutex);

    if (g_InstalledMaps.IsEmpty())
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

    sq_newarray(v, 0);

    FOR_EACH_VEC(g_InstalledMaps, i)
    {
        const CUtlString& mapName = g_InstalledMaps[i];

        sq_pushstring(v, mapName.String(), (SQInteger)mapName.Length());
        sq_arrayappend(v, -2);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: return all available playlists
//-----------------------------------------------------------------------------
static SQRESULT SharedScript_GetAvailablePlaylists(HSQUIRRELVM v)
{
    if (g_vecAllPlaylists.IsEmpty())
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

    sq_newarray(v, 0);
    for (const CUtlString& it : g_vecAllPlaylists)
    {
        sq_pushstring(v, it.String(), (SQInteger)it.Length());
        sq_arrayappend(v, -2);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: forces a script error
//-----------------------------------------------------------------------------
static SQRESULT SharedScript_ScriptError(HSQUIRRELVM v)
{
    SQChar* pString = NULL;
    SQInteger nLen = 0;

    if (v_sqstd_format(v, 0, SQTrue, &nLen, &pString) < 0)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    v_SQVM_ScriptError("%s", pString);
    SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
}

//-----------------------------------------------------------------------------
// Purpose: alias for engine's GetDatatableRowCount (lowercase 't') to match
//          script naming convention GetDataTableRowCount (uppercase 'T')
//-----------------------------------------------------------------------------
static SQRESULT SharedScript_GetDataTableRowCount(HSQUIRRELVM v)
{
	return (SQRESULT)v_Script_GetDatatableRowCount((__int64)v);
}

//-----------------------------------------------------------------------------
// Seeded random number generation
//
// Integer handles mapped to a static pool of CUniformRandomStream instances.
// Scripts treat the handle as an opaque 'var'.
//-----------------------------------------------------------------------------
static constexpr int SEEDED_RANDOM_POOL_SIZE = 128;
static CUniformRandomStream s_seededRandomPool[SEEDED_RANDOM_POOL_SIZE];
static int s_nSeededRandomCount = 0;

static CUniformRandomStream* SeededRandom_GetStream(HSQUIRRELVM v)
{
    SQInteger nHandle;
    if (sq_getinteger(v, 2, &nHandle) != SQ_OK
        || nHandle < 0
        || nHandle >= SEEDED_RANDOM_POOL_SIZE
        || nHandle >= s_nSeededRandomCount)
    {
        v_SQVM_RaiseError(v, "First argument is not a random seed");
        return nullptr;
    }
    return &s_seededRandomPool[nHandle];
}

static int SeededRandom_ClampInt(const SQInteger val)
{
    if (val > INT_MAX) return INT_MAX;
    if (val < INT_MIN) return INT_MIN;
    return static_cast<int>(val);
}

//-----------------------------------------------------------------------------
static SQRESULT SharedScript_CreateRandomSeed(HSQUIRRELVM v)
{
    SQInteger nSeed;
    sq_getinteger(v, 2, &nSeed);

    if (s_nSeededRandomCount >= SEEDED_RANDOM_POOL_SIZE)
    {
        v_SQVM_RaiseError(v, "CreateRandomSeed: pool exhausted (%d max)", SEEDED_RANDOM_POOL_SIZE);
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    const int nHandle = s_nSeededRandomCount++;
    s_seededRandomPool[nHandle].SetSeed(SeededRandom_ClampInt(nSeed));

    sq_pushinteger(v, nHandle);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
static SQRESULT SharedScript_RandomFloatSeeded(HSQUIRRELVM v)
{
    CUniformRandomStream* pStream = SeededRandom_GetStream(v);
    if (!pStream)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    SQFloat flMax;
    sq_getfloat(v, 3, &flMax);

    sq_pushfloat(v, pStream->RandomFloat(0.0f, flMax));
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
static SQRESULT SharedScript_RandomFloatRangeSeeded(HSQUIRRELVM v)
{
    CUniformRandomStream* pStream = SeededRandom_GetStream(v);
    if (!pStream)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    SQFloat flMin, flMax;
    sq_getfloat(v, 3, &flMin);
    sq_getfloat(v, 4, &flMax);

    sq_pushfloat(v, pStream->RandomFloat(flMin, flMax));
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
static SQRESULT SharedScript_RandomIntSeeded(HSQUIRRELVM v)
{
    CUniformRandomStream* pStream = SeededRandom_GetStream(v);
    if (!pStream)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    SQInteger nMax;
    sq_getinteger(v, 3, &nMax);

    if (nMax < 1)
    {
        v_SQVM_RaiseError(v, "RandomInt: max value (%d) must be greater than 0", nMax);
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    sq_pushinteger(v, pStream->RandomInt(0, SeededRandom_ClampInt(nMax) - 1));
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
static SQRESULT SharedScript_RandomIntRangeSeeded(HSQUIRRELVM v)
{
    CUniformRandomStream* pStream = SeededRandom_GetStream(v);
    if (!pStream)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    SQInteger nMin, nMax;
    sq_getinteger(v, 3, &nMin);
    sq_getinteger(v, 4, &nMax);

    if (nMax <= nMin)
    {
        v_SQVM_RaiseError(v, "RandomIntRange: min value (%d) must be less than max value (%d)", nMin, nMax);
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    sq_pushinteger(v, pStream->RandomInt(SeededRandom_ClampInt(nMin), SeededRandom_ClampInt(nMax) - 1));
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
static SQRESULT SharedScript_RandomIntRangeInclusiveSeeded(HSQUIRRELVM v)
{
    CUniformRandomStream* pStream = SeededRandom_GetStream(v);
    if (!pStream)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    SQInteger nMin, nMax;
    sq_getinteger(v, 3, &nMin);
    sq_getinteger(v, 4, &nMax);

    if (nMax < nMin)
    {
        v_SQVM_RaiseError(v, "RandomIntRangeInclusive: min value (%d) must be less than or equal to max value (%d)", nMin, nMax);
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    sq_pushinteger(v, pStream->RandomInt(SeededRandom_ClampInt(nMin), SeededRandom_ClampInt(nMax)));
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//---------------------------------------------------------------------------------
// Purpose: common script abstractions
// Input  : *s -
//---------------------------------------------------------------------------------
void Script_RegisterCommonAbstractions(CSquirrelVM* s)
{
    DEFINE_SHARED_SCRIPTFUNC_NAMED(s, GetSDKVersion, "Gets the SDK version as a string", "string", "", false);

    DEFINE_SHARED_SCRIPTFUNC_NAMED(s, GetAvailableMaps, "Gets an array of all available maps", "array< string >", "", false);
    DEFINE_SHARED_SCRIPTFUNC_NAMED(s, GetAvailablePlaylists, "Gets an array of all available playlists", "array< string >", "", false);

    DEFINE_SHARED_SCRIPTFUNC_NAMED(s, ScriptError, "Throws a script error", "void", "string format, ...", true);

    // Scripts use GetDataTableRowCount (uppercase T), engine registers GetDatatableRowCount (lowercase t)
    DEFINE_SHARED_SCRIPTFUNC_NAMED(s, GetDataTableRowCount, "Returns the number of rows in the datatable", "int", "var datatable", false);

    // Seeded random number generation
    DEFINE_SHARED_SCRIPTFUNC_NAMED(s, CreateRandomSeed, "Generate a random seed by a given integer", "var", "int seed", false);
    DEFINE_SHARED_SCRIPTFUNC_NAMED(s, RandomFloatSeeded, "Generate a random floating point number from a seed", "float", "var seed, float max", false);
    DEFINE_SHARED_SCRIPTFUNC_NAMED(s, RandomFloatRangeSeeded, "Generate a random floating point number within a range from a seed", "float", "var seed, float min, float max", false);
    DEFINE_SHARED_SCRIPTFUNC_NAMED(s, RandomIntSeeded, "Generate a random integer from a seed", "int", "var seed, int max", false);
    DEFINE_SHARED_SCRIPTFUNC_NAMED(s, RandomIntRangeSeeded, "Generate a random integer within a range from a seed", "int", "var seed, int min, int max", false);
    DEFINE_SHARED_SCRIPTFUNC_NAMED(s, RandomIntRangeInclusiveSeeded, "Generate a random integer within an inclusive range from a seed", "int", "var seed, int min, int max", false);

    Script_RegisterModSystemFunctions(s);

    // NOTE: plugin functions must always come after SDK functions!
    for (auto& callback : !PluginSystem()->GetRegisterSharedScriptFuncsCallbacks())
    {
        // Register script functions inside plugins.
        callback.Function()(s);
    }
}

//---------------------------------------------------------------------------------
// Purpose: listen server constants
// Input  : *s - 
//---------------------------------------------------------------------------------
void Script_RegisterListenServerConstants(CSquirrelVM* s)
{
    const SQBool hasListenServer = !IsClientDLL();
    s->RegisterConstant("LISTEN_SERVER", hasListenServer);
}

//---------------------------------------------------------------------------------
// Purpose: server enums
// Input  : *s - 
//---------------------------------------------------------------------------------
static void Script_RegisterCommonEnums_Server(CSquirrelVM* const s)
{
    v_Script_RegisterCommonEnums_Server(s);

    if (ServerScriptRegisterEnum_Callback)
        ServerScriptRegisterEnum_Callback(s);
}

//---------------------------------------------------------------------------------
// Purpose: client/ui enums
// Input  : *s - 
//---------------------------------------------------------------------------------
static void Script_RegisterCommonEnums_Client(CSquirrelVM* const s)
{
    v_Script_RegisterCommonEnums_Client(s);

    const SQCONTEXT context = s->GetContext();

    if (context == SQCONTEXT::CLIENT && ClientScriptRegisterEnum_Callback)
        ClientScriptRegisterEnum_Callback(s);
    else if (context == SQCONTEXT::UI && UIScriptRegisterEnum_Callback)
        UIScriptRegisterEnum_Callback(s);
}

//---------------------------------------------------------------------------------
// Purpose: null-guard workaround for SQObject_ToString crash during remote
//          function registration (srcObj or dstObj can be NULL in some paths)
//---------------------------------------------------------------------------------
static __int64 SQObject_ToString(__int64 vm, void* srcObj, void* dstObj)
{
    if (!dstObj)
        return 0;

    if (!srcObj)
    {
        // Substitute a valid OT_NULL SQObject so the original produces "null"
        // instead of crashing. The caller expects a valid string in dstObj.
        static __declspec(align(16)) char s_NullObj[16] = {};
        *reinterpret_cast<int*>(s_NullObj) = 0x01000001; // OT_NULL
        srcObj = s_NullObj;
    }

    return v_SQObject_ToString(vm, srcObj, dstObj);
}

void VScriptShared::Detour(const bool bAttach) const
{
    DetourSetup(&v_Script_RegisterCommonEnums_Server, &Script_RegisterCommonEnums_Server, bAttach);
    DetourSetup(&v_Script_RegisterCommonEnums_Client, &Script_RegisterCommonEnums_Client, bAttach);

    if (v_SQObject_ToString)
        DetourSetup(&v_SQObject_ToString, &SQObject_ToString, bAttach);
}