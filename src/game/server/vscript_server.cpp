//=============================================================================//
// 
// Purpose: Expose native code to VScript API
// 
//-----------------------------------------------------------------------------
// 
// Read the documentation in 'game/shared/vscript_shared.cpp' before modifying
// existing code or adding new code!
// 
// To create server script bindings:
// - use the DEFINE_SERVER_SCRIPTFUNC_NAMED() macro.
// - prefix your function with "ServerScript_" i.e.: "ServerScript_GetVersion".
// 
//=============================================================================//

#include "core/stdafx.h"
#include "common/callback.h"
#include "engine/server/server.h"
#include "engine/host_state.h"
#include "engine/debugoverlay.h"
#include "pluginsystem/pluginsystem.h"
#include "vscript/vscript.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"

#include "game/shared/vscript_gamedll_defs.h"
#include "game/shared/globalnonrewind_vars.h"
#include "game/shared/weapon_heat.h"
#include "game/shared/deathfield_system.h"
#include "game/shared/highlight_context.h"

#include "game/shared/vscript_shared.h"
#include "game/shared/vscript_debug_overlay_shared.h"

#include "liveapi/liveapi.h"
#include "vscript_server.h"
#include "player.h"
#include "detour_impl.h"
#include "game/shared/weapon_script_vars.h"
#include "game/shared/status_effects_sdk.h"
#include "game/client/vscript_player.h"
#include "game/client/vscript_remotefunctions.h"

/*
=====================
SQVM_ServerScript_f

  Executes input on the
  VM in SERVER context.
=====================
*/
static void SQVM_ServerScript_f(const CCommand& args)
{
    if (args.ArgC() >= 2)
    {
        Script_Execute(args.ArgS(), SQCONTEXT::SERVER);
    }
}
static ConCommand script("script", SQVM_ServerScript_f, "Run input code as SERVER script on the VM", FCVAR_DEVELOPMENTONLY | FCVAR_GAMEDLL | FCVAR_CHEAT | FCVAR_SERVER_FRAME_THREAD);

//-----------------------------------------------------------------------------
// Purpose: server NDebugOverlay proxies
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_DebugDrawSolidBox(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawSolidBox(v);
}
static SQRESULT ServerScript_DebugDrawSweptBox(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawSweptBox(v);
}
static SQRESULT ServerScript_DebugDrawTriangle(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawTriangle(v);
}
static SQRESULT ServerScript_DebugDrawSolidSphere(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawSolidSphere(v);
}
static SQRESULT ServerScript_DebugDrawCapsule(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawCapsule(v);
}
static SQRESULT ServerScript_CreateBox(HSQUIRRELVM v)
{
    return SharedScript_CreateBox(v);
}
static SQRESULT ServerScript_ClearBoxes(HSQUIRRELVM v)
{
    return SharedScript_ClearBoxes(v);
}

//-----------------------------------------------------------------------------
// Purpose: calculates the duration for the debug text overlay
//-----------------------------------------------------------------------------
static float ServerScript_DebugScreenText_DetermineDuration(HSQUIRRELVM v)
{
    const float serverFPS = script_server_fps->GetFloat();
    // Make sure the overlay exists as long as the entire server
    // script frame, as it must last until the next call from the
    // server is initiated. Otherwise the following happens:
    // 
    // - 1 / script_server_fps < NDEBUG_PERSIST_TILL_NEXT_SERVER =
    //                           text will flicker as they decay
    //                           before the next frame is fired.
    // - 1 / script_server_fps > NDEBUG_PERSIST_TILL_NEXT_SERVER =
    //                           text will overlap with previous
    //                           as the prev hasn't decayed yet.
    return 1.0f / serverFPS;
}

//-----------------------------------------------------------------------------
// Purpose: internal handler for adding debug texts on screen through scripts
//-----------------------------------------------------------------------------
static void ServerScript_Internal_DebugScreenTextWithColor(HSQUIRRELVM v, const float posX, const float posY, const Color color, const char* const text)
{
    const float duration = ServerScript_DebugScreenText_DetermineDuration(v);
    g_pDebugOverlay->AddScreenTextOverlay(posX, posY, duration, color.r(), color.g(), color.b(), color.a(), text);
}

//-----------------------------------------------------------------------------
// Purpose: adds a debug text on the screen at given position
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_DebugScreenText(HSQUIRRELVM v)
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
        ServerScript_Internal_DebugScreenTextWithColor(v, posX, posY, color, text);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: adds a debug text on the screen at given position with color
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_DebugScreenTextWithColor(HSQUIRRELVM v)
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
        ServerScript_Internal_DebugScreenTextWithColor(v, posX, posY, color, text);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: sets whether the server could auto reload at this time (e.g. if
// server admin has host_autoReloadRate AND host_autoReloadRespectGameState
// set, and its time to auto reload, but the match hasn't finished yet, wait
// until this is set to proceed the reload of the server
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_SetAutoReloadState(HSQUIRRELVM v)
{
    SQBool state = false;
    sq_getbool(v, 2, &state);

    g_hostReloadState = state;
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: kicks a player by given name
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_KickPlayerByName(HSQUIRRELVM v)
{
    const SQChar* playerName = nullptr;
    const SQChar* reason = nullptr;

    sq_getstring(v, 2, &playerName);
    sq_getstring(v, 3, &reason);

    if (!VALID_CHARSTAR(playerName))
    {
        v_SQVM_ScriptError("Empty or null player name");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Discard empty strings, this will use the default message instead.
    if (!VALID_CHARSTAR(reason))
        reason = nullptr;

    g_BanSystem.KickPlayerByName(playerName, reason);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: kicks a player by given handle or id
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_KickPlayerById(HSQUIRRELVM v)
{
    const SQChar* playerHandle = nullptr;
    const SQChar* reason = nullptr;

    sq_getstring(v, 2, &playerHandle);
    sq_getstring(v, 3, &reason);

    if (!VALID_CHARSTAR(playerHandle))
    {
        v_SQVM_ScriptError("Empty or null player handle");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Discard empty strings, this will use the default message instead.
    if (!VALID_CHARSTAR(reason))
        reason = nullptr;

    g_BanSystem.KickPlayerById(playerHandle, reason);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: bans a player by given name
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_BanPlayerByName(HSQUIRRELVM v)
{
    const SQChar* playerName = nullptr;
    const SQChar* reason = nullptr;

    sq_getstring(v, 2, &playerName);
    sq_getstring(v, 3, &reason);

    if (!VALID_CHARSTAR(playerName))
    {
        v_SQVM_ScriptError("Empty or null player name");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Discard empty strings, this will use the default message instead.
    if (!VALID_CHARSTAR(reason))
        reason = nullptr;

    g_BanSystem.BanPlayerByName(playerName, reason);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: bans a player by given handle or id
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_BanPlayerById(HSQUIRRELVM v)
{
    const SQChar* playerHandle = nullptr;
    const SQChar* reason = nullptr;

    sq_getstring(v, 2, &playerHandle);
    sq_getstring(v, 3, &reason);

    if (!VALID_CHARSTAR(playerHandle))
    {
        v_SQVM_ScriptError("Empty or null player handle");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Discard empty strings, this will use the default message instead.
    if (!VALID_CHARSTAR(reason))
        reason = nullptr;

    g_BanSystem.BanPlayerById(playerHandle, reason);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: unbans a player by given Steam ID or ip address
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_UnbanPlayer(HSQUIRRELVM v)
{
    const SQChar* szCriteria = nullptr;
    sq_getstring(v, 2, &szCriteria);

    if (!VALID_CHARSTAR(szCriteria))
    {
        v_SQVM_ScriptError("Empty or null player criteria");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    g_BanSystem.UnbanPlayer(szCriteria);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT ServerScript_BroadcastServerTextMessage(HSQUIRRELVM v)
{
    const SQChar* pszPrefix = nullptr;
    const SQChar* pszMessage = nullptr;
    SQBool bAdminMsg = false;

    sq_getstring(v, 2, &pszPrefix);
    sq_getstring(v, 3, &pszMessage);
    sq_getbool(v, 4, &bAdminMsg);

    if (!VALID_CHARSTAR(pszPrefix))
    {
        v_SQVM_ScriptError("Null prefix string");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    if (!VALID_CHARSTAR(pszMessage))
    {
        v_SQVM_ScriptError("Null message string");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    SVC_SystemSayText message(pszPrefix, pszMessage, bAdminMsg);

    g_pServer->BroadcastMessage(&message, true, false);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT ServerScript_SendServerTextMessage(HSQUIRRELVM v)
{
    CPlayer* pPlayer = nullptr;
    const SQChar* pszPrefix = nullptr;
    const SQChar* pszMessage = nullptr;
    SQBool bAdminMsg = false;

    if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
        return SQ_ERROR;

    sq_getstring(v, 2, &pszPrefix);
    sq_getstring(v, 3, &pszMessage);
    sq_getbool(v, 4, &bAdminMsg);

    if (!VALID_CHARSTAR(pszPrefix))
    {
        v_SQVM_ScriptError("Null prefix string");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    if (!VALID_CHARSTAR(pszMessage))
    {
        v_SQVM_ScriptError("Null message string");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    CClient* const pClient = g_pServer->GetClient(pPlayer->GetEdict() - 1);

    if (!pClient)
        return SQ_ERROR;

    SVC_SystemSayText message(pszPrefix, pszMessage, bAdminMsg);

    sq_pushbool(v, pClient->SendNetMsgEx(&message, false, false, false));
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

// Chat Builder API - gives complete control over chat rendering
// Format: "CMD|data|CMD|data|..."
// Commands: N=newline, T=text, C=color(r,g,b), F=fade(dur,fade)

static SQRESULT ServerScript_ChatBuilder(HSQUIRRELVM v)
{
    CPlayer* pPlayer = nullptr;
    const SQChar* pszCommands = nullptr;

    if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
        return SQ_ERROR;

    sq_getstring(v, 2, &pszCommands);

    if (!VALID_CHARSTAR(pszCommands))
    {
        v_SQVM_ScriptError("Null commands string");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    CClient* const pClient = g_pServer->GetClient(pPlayer->GetEdict() - 1);

    if (!pClient)
        return SQ_ERROR;

    // Prefix commands with special marker in message field
    char szMarkedCommands[256];
    V_snprintf(szMarkedCommands, sizeof(szMarkedCommands), "~~~CB~~~%s", pszCommands);

    // Send with empty prefix
    SVC_SystemSayText message("", szMarkedCommands, true);

    sq_pushbool(v, pClient->SendNetMsgEx(&message, false, false, false));
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

// Broadcast ChatBuilder message to all players
static SQRESULT ServerScript_BroadcastChatBuilder(HSQUIRRELVM v)
{
    const SQChar* pszCommands = nullptr;
    sq_getstring(v, 2, &pszCommands);

    if (!VALID_CHARSTAR(pszCommands))
    {
        v_SQVM_ScriptError("Null commands string");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Prefix commands with special marker in message field
    char szMarkedCommands[256];
    V_snprintf(szMarkedCommands, sizeof(szMarkedCommands), "~~~CB~~~%s", pszCommands);

    // Send with empty prefix
    SVC_SystemSayText message("", szMarkedCommands, true);

    g_pServer->BroadcastMessage(&message, true, false);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT ServerScript_ChatBuilderRainbow(HSQUIRRELVM v)
{
    CPlayer* pPlayer = nullptr;
    const SQChar* pszText = nullptr;
    SQFloat flDuration = 5.0f;
    SQFloat flFadeTime = 1.0f;

    if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
        return SQ_ERROR;

    sq_getstring(v, 2, &pszText);
    sq_getfloat(v, 3, &flDuration);
    sq_getfloat(v, 4, &flFadeTime);

    if (!VALID_CHARSTAR(pszText))
    {
        v_SQVM_ScriptError("Null text string");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    CClient* const pClient = g_pServer->GetClient(pPlayer->GetEdict() - 1);
    if (!pClient)
        return SQ_ERROR;

    // Build simplified command using client-side 'R' command
    // Format: "N|F|duration,fade|R|text|"
    char szCommands[512];
    int written = snprintf(szCommands, sizeof(szCommands), "N|F|%.1f,%.1f|R|%s|",
        flDuration, flFadeTime, pszText);

    if (written < 0 || written >= sizeof(szCommands))
    {
        v_SQVM_ScriptError("Command buffer overflow - text too long");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Send the message
    char szMarkedCommands[512];
    V_snprintf(szMarkedCommands, sizeof(szMarkedCommands), "~~~CB~~~%s", szCommands);

    SVC_SystemSayText message("", szMarkedCommands, true);
    sq_pushbool(v, pClient->SendNetMsgEx(&message, false, false, false));
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets the number of real players on this server
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_GetNumHumanPlayers(HSQUIRRELVM v)
{
    sq_pushinteger(v, g_pServer->GetNumHumanPlayers());
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets the number of fake players on this server
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_GetNumFakeClients(HSQUIRRELVM v)
{
    sq_pushinteger(v, g_pServer->GetNumFakeClients());
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets our current session id
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_GetSessionID(HSQUIRRELVM v)
{
    sq_pushstring(v, g_LogSessionUUID.c_str(), (SQInteger)g_LogSessionUUID.length());
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: creates a fake player and returns the edict index
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_CreateFakePlayer(HSQUIRRELVM v)
{
    if (!g_pServer->IsActive())
    {
        sq_pushinteger(v, -1);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    const int numPlayers = g_pServer->GetNumClients();

    // Already at max, don't create
    if (numPlayers >= g_ServerGlobalVariables->maxClients)
    {
        sq_pushinteger(v, -1);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    const SQChar* playerName;
    SQInteger teamNum;

    // Get parameters
    sq_getstring(v, 2, &playerName);
    sq_getinteger(v, 3, &teamNum);

    if (!VALID_CHARSTAR(playerName))
    {
        v_SQVM_ScriptError("Empty or null player name");
        sq_pushinteger(v, -1);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Thread synchronization required
    ThreadJoinServerJob();

    // Create the fake client
    const edict_t nHandle = g_pEngineServer->CreateFakeClient(playerName, static_cast<int>(teamNum));

    if (nHandle < 0)
    {
        sq_pushinteger(v, -1);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Fully connect the client
    g_pServerGameClients->ClientFullyConnect(nHandle, false);

    // Return the edict index - scripts can use GetPlayerArray() or GetPlayerByIndex() to get the entity
    sq_pushinteger(v, static_cast<SQInteger>(nHandle));
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: sets a class var on the server and each client
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_ScriptSetClassVar(HSQUIRRELVM v)
{
    CPlayer* player = nullptr;

    if (!v_sq_getentity(v, (SQEntity*)&player))
        return SQ_ERROR;

    const SQChar* key = nullptr;
    sq_getstring(v, 2, &key);

    if (!VALID_CHARSTAR(key))
    {
        v_SQVM_ScriptError("Empty or null class key");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    const SQChar* val = nullptr;
    sq_getstring(v, 3, &val);

    if (!VALID_CHARSTAR(val))
    {
        v_SQVM_ScriptError("Empty or null class value");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    CClient* const client = g_pServer->GetClient(player->GetEdict() - 1);
    SVC_SetClassVar msg(key, val);

    const bool success = client->SendNetMsgEx(&msg, false, true, false);

    if (success)
    {
        const char* pArgs[3] = {
            "_setClassVarServer",
            key,
            val
        };

        const CCommand cmd((int)V_ARRAYSIZE(pArgs), pArgs, cmd_source_t::kCommandSrcCode);
        const int oldIdx = *g_nCommandClientIndex;

        *g_nCommandClientIndex = client->GetUserID();
        v__setClassVarServer_f(cmd);

        *g_nCommandClientIndex = oldIdx;
    }

    sq_pushbool(v, success);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: checks if the provided hull type is valid
//-----------------------------------------------------------------------------
static bool Internal_ServerScript_ValidateHull(const SQInteger hull)
{
    if (hull < 0 || hull >= Hull_e::NUM_HULLS)
    {
        v_SQVM_ScriptError("Hull type with value %d does not index the maximum number of hulls(%d)", hull, Hull_e::NUM_HULLS);
        return false;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: checks if the provided half-extents is valid
//-----------------------------------------------------------------------------
static bool Internal_ServerScript_NavMesh_GetExtents(HSQUIRRELVM v, const SQInteger stackIdx, rdVec3D* const out)
{
    const SQVector3D* extents;
    sq_getvector(v, stackIdx, &extents);

    const SQFloat maxMagnitudeSqr = 9000000.0f;
    const SQFloat magnitudeSqr = extents->Dot();

    if (magnitudeSqr > maxMagnitudeSqr)
    {
        v_SQVM_ScriptError("Extents magnitude (%f) is too big. Max magnitude is %f", sqrtf(magnitudeSqr), sqrtf(maxMagnitudeSqr));
        return false;
    }

    if (extents->x <= 0.f || extents->y <= 0.f || extents->z <= 0.f)
    {
        v_SQVM_ScriptError("Extents elements (%f, %f, %f) must all be greater than zero", extents->x, extents->y, extents->z);
        return false;
    }

    out->init(extents->x, extents->y, extents->z);
    return true;
}

//-----------------------------------------------------------------------------
// Purpose: finds the nearest poly to given point, with an optional bounds filter.
//-----------------------------------------------------------------------------
static bool Internal_ServerScript_NavMesh_FindNearestPos(HSQUIRRELVM v, const bool useBounds)
{
    SQInteger hullIdx;
    sq_getinteger(v, useBounds ? 4 : 3, &hullIdx);

    if (!Internal_ServerScript_ValidateHull(hullIdx))
        return false;

    const Hull_e hullType = Hull_e(hullIdx);

    const NavMeshType_e navType = NAI_Hull::NavMeshType(hullType);
    const dtNavMesh* const nav = Detour_GetNavMeshByType(navType);

    if (!nav)
    {
        v_SQVM_ScriptError("NavMesh \"%s\" for hull \"%s\" hasn't been loaded!",
            NavMesh_GetNameForType(navType), g_aiHullNames[hullType]);

        return false;
    }

    rdVec3D halfExtents;

    if (useBounds)
    {
        if (!Internal_ServerScript_NavMesh_GetExtents(v, 3, &halfExtents))
            return false;
    }
    else
    {
        const Vector3D& maxs = NAI_Hull::Maxs(hullType);
        halfExtents.init(maxs.x, maxs.y, maxs.z);
    }

    const SQVector3D* point;
    sq_getvector(v, 2, &point);

    const rdVec3D searchPoint(point->x, point->y, point->z);

    dtNavMeshQuery query;
    query.attachNavMeshUnsafe(nav);

    dtQueryFilter filter;
    filter.setIncludeFlags(DT_POLYFLAGS_ALL);
    filter.setExcludeFlags(DT_POLYFLAGS_DISABLED);

    dtPolyRef nearestRef;
    rdVec3D nearestPt;

    const dtStatus status = query.findNearestPoly(&searchPoint, &halfExtents, &filter, &nearestRef, &nearestPt);

    if (dtStatusFailed(status) || !nearestRef)
    {
        v->PushNull();
        return true;
    }

    const SQVector3D result(nearestPt.x, nearestPt.y, nearestPt.z);
    sq_pushvector(v, &result);

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: finds the nearest polygon to provided point
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_GetNearestPos(HSQUIRRELVM v)
{
    const bool ret = Internal_ServerScript_NavMesh_FindNearestPos(v, false);

    if (!ret)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: finds the nearest polygon to provided point within extents
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_GetNearestPosInBounds(HSQUIRRELVM v)
{
    const bool ret = Internal_ServerScript_NavMesh_FindNearestPos(v, true);

    if (!ret)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: saves a recorded animation on the disk to be used by bakery
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_SaveRecordedAnimation(HSQUIRRELVM v)
{
    if (!developer->GetBool())
    {
        v_SQVM_ScriptError("SaveRecordedAnimation() is dev only!");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    AnimRecordingAssetHeader_s* const animRecording = v_ServerScript_GetRecordedAnimationFromCurrentStack(v);

    if (!animRecording)
    {
        v_SQVM_ScriptError("Parameter must be a recorded animation");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    if (animRecording->numRecordedFrames == 0)
    {
        v_SQVM_ScriptError("Recorded animation has 0 frames");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    const SQChar* fileName;
    sq_getstring(v, 3, &fileName);

    char fileNameBuf[MAX_OSPATH];
    const int fmtResult = snprintf(fileNameBuf, sizeof(fileNameBuf), "anim_recording/%s.anir", fileName);

    if (fmtResult < 0)
    {
        v_SQVM_ScriptError("Failed to format recorded animation file name; provided name \"%s\" is invalid", fileName);
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    FileSystem()->CreateDirHierarchy("anim_recording/", "MOD");
    FileHandle_t animRecordingFile = FileSystem()->Open(fileNameBuf, "wb", "MOD");

    if (animRecordingFile == FILESYSTEM_INVALID_HANDLE)
    {
        v_SQVM_ScriptError("Failed to open recorded animation file \"%s\" for write", fileNameBuf);
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    AnimRecordingFileHeader_s fileHdr;

    fileHdr.magic = ANIR_FILE_MAGIC;
    fileHdr.fileVersion = ANIR_FILE_VERSION;
    fileHdr.assetVersion = ANIR_ASSET_VERSION;

    fileHdr.startPos = animRecording->startPos;
    fileHdr.startAngles = animRecording->startAngles;

    fileHdr.stringBufSize = 0;

    fileHdr.numElements = 0;
    fileHdr.numSequences = 0;

    fileHdr.numRecordedFrames = animRecording->numRecordedFrames;
    fileHdr.numRecordedOverlays = animRecording->numRecordedOverlays;

    fileHdr.animRecordingId = animRecording->animRecordingId;
    FileSystem()->Write(&fileHdr, sizeof(AnimRecordingFileHeader_s), animRecordingFile);

    // This information can only be retrieved by counting the number
    // of valid pose parameter names.
    int numElems = 0;
    int stringBufLen = 0;

    // Write out the pose parameters.
    for (int i = 0; i < ANIR_MAX_ELEMENTS; i++)
    {
        const char* const poseParamName = animRecording->poseParamNames[i];

        if (poseParamName)
            numElems++;
        else
            break;

        const ssize_t strLen = (ssize_t)strlen(poseParamName) + 1; // Include the null too.
        FileSystem()->Write(poseParamName, strLen, animRecordingFile);

        stringBufLen += (int)strLen;
    }

    // Write out the pose values.
    for (int i = 0; i < numElems; i++)
    {
        const Vector2D* poseParamValue = &animRecording->poseParamValues[i];
        FileSystem()->Write(poseParamValue, sizeof(Vector2D), animRecordingFile);
    }

    int numSeqs = 0;

    // Write out the animation sequence names.
    for (int i = 0; i < ANIR_MAX_SEQUENCES; i++)
    {
        const char* const animSequenceName = animRecording->animSequences[i];

        if (animSequenceName)
            numSeqs++;
        else
            break;

        const ssize_t strLen = (ssize_t)strlen(animSequenceName) + 1; // Include the null too.
        FileSystem()->Write(animSequenceName, strLen, animRecordingFile);

        stringBufLen += (int)strLen;
    }

    // Write out the recorded frames.
    for (int i = 0; i < animRecording->numRecordedFrames; i++)
    {
        assert(animRecording->recordedFrames);

        const AnimRecordingFrame_s* const frame = &animRecording->recordedFrames[i];
        FileSystem()->Write(frame, sizeof(AnimRecordingFrame_s), animRecordingFile);
    }

    // Write out the recorded overlays.
    for (int i = 0; i < animRecording->numRecordedOverlays; i++)
    {
        assert(animRecording->recordedOverlays);

        const AnimRecordingOverlay_s* const overlay = &animRecording->recordedOverlays[i];
        FileSystem()->Write(overlay, sizeof(AnimRecordingOverlay_s), animRecordingFile);
    }

    // Update the data in the header if we ended up writing
    // elements and sequences.
    if (numElems > 0 || numSeqs > 0)
    {
        FileSystem()->Seek(animRecordingFile, offsetof(AnimRecordingFileHeader_s, stringBufSize), FILESYSTEM_SEEK_HEAD);

        FileSystem()->Write(&stringBufLen, sizeof(int), animRecordingFile);
        FileSystem()->Write(&numElems, sizeof(int), animRecordingFile);
        FileSystem()->Write(&numSeqs, sizeof(int), animRecordingFile);
    }

    FileSystem()->Close(animRecordingFile);

    Msg(eDLL_T::SERVER, "Recorded animation saved to \"%s\"\n", fileNameBuf);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//---------------------------------------------------------------------------------
// Purpose: registers script functions in SERVER context
// Input  : *s - 
//---------------------------------------------------------------------------------
void Script_RegisterServerFunctions(CSquirrelVM* s)
{
    Script_RegisterCommonAbstractions(s);
    Script_RegisterCoreServerFunctions(s);
    Script_RegisterAdminServerFunctions(s);

    Script_RegisterLiveAPIFunctions(s);

    // NOTE: plugin functions must always come after SDK functions!
    for (auto& callback : !PluginSystem()->GetRegisterServerScriptFuncsCallbacks())
    {
        // Register script functions inside plugins.
        callback.Function()(s);
    }
}

void Script_RegisterServerEnums(CSquirrelVM* const s)
{
    Script_RegisterLiveAPIEnums(s);
}

//---------------------------------------------------------------------------------
// Purpose: core server script functions
// Input  : *s - 
//---------------------------------------------------------------------------------
void Script_RegisterCoreServerFunctions(CSquirrelVM* s)
{
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, DebugDrawSolidBox, "Draw a debug overlay solid box", "void", "vector origin, vector mins, vector maxs, vector color, float alpha, bool drawThroughWorld, float duration", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, DebugDrawSweptBox, "Draw a debug overlay swept box", "void", "vector start, vector end, vector mins, vector maxs, vector angles, vector color, float alpha, bool drawThroughWorld, float duration", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, DebugDrawTriangle, "Draw a debug overlay triangle", "void", "vector p1, vector p2, vector p3, vector color, float alpha, bool drawThroughWorld, float duration", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, DebugDrawSolidSphere, "Draw a debug overlay solid sphere", "void", "vector origin, float radius, int theta, int phi, vector color, float alpha, bool drawThroughWorld, float duration", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, DebugDrawCapsule, "Draw a debug overlay capsule", "void", "vector start, vector end, float radius, vector color, float alpha, bool drawThroughWorld, float duration", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, CreateBox, "Create a permanent box for map making", "void", "vector origin, vector angles, vector mins, vector maxs, vector color, float alpha", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, ClearBoxes, "Clear all debug overlays and boxes", "void", "", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, SetAutoReloadState, "Set whether we can auto-reload the server", "void", "bool canAutoReload", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, GetSessionID, "Gets our current session ID", "string", "", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_GetNearestPos, "Finds the nearest position to the provided point on the hull's NavMesh using the hull's bounds as extents", "vector ornull", "vector searchPoint, int hullType", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_GetNearestPosInBounds, "Finds the nearest position to the provided point on the hull's NavMesh using provided bounds as extents", "vector ornull", "vector searchPoint, vector halfExtents, int hullType", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, SaveRecordedAnimation, "Saves an anim_recording asset to be used by bakery. (dev only)", "void", "var recordedAnim, string fileName", false);

    Script_RegisterRemoteFunctionServerNatives(s);

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

    StatusEffects_SDK_RegisterServerFunctions(s);

    // GlobalNonRewind variable system
    Script_RegisterFuncNamed(s, "SetGlobalNonRewindNetBool", "Script_SetGlobalNonRewindNetBool", "Sets a global non-rewind bool", "void", "string name, bool value", false, Script_SetGlobalNonRewindNetBool);
    Script_RegisterFuncNamed(s, "SetGlobalNonRewindNetInt", "Script_SetGlobalNonRewindNetInt", "Sets a global non-rewind int", "void", "string name, int value", false, Script_SetGlobalNonRewindNetInt);
    Script_RegisterFuncNamed(s, "SetGlobalNonRewindNetFloat", "Script_SetGlobalNonRewindNetFloat", "Sets a global non-rewind float", "void", "string name, float value", false, Script_SetGlobalNonRewindNetFloat);
    Script_RegisterFuncNamed(s, "SetGlobalNonRewindNetTime", "Script_SetGlobalNonRewindNetTime", "Sets a global non-rewind time", "void", "string name, float value", false, Script_SetGlobalNonRewindNetTime);
    Script_RegisterFuncNamed(s, "GetGlobalNonRewindNetBool", "Script_GetGlobalNonRewindNetBool", "Gets a global non-rewind bool", "bool", "string name", false, Script_GetGlobalNonRewindNetBool);
    Script_RegisterFuncNamed(s, "GetGlobalNonRewindNetInt", "Script_GetGlobalNonRewindNetInt", "Gets a global non-rewind int", "int", "string name", false, Script_GetGlobalNonRewindNetInt);
    Script_RegisterFuncNamed(s, "GetGlobalNonRewindNetFloat", "Script_GetGlobalNonRewindNetFloat", "Gets a global non-rewind float", "float", "string name", false, Script_GetGlobalNonRewindNetFloat);
    Script_RegisterFuncNamed(s, "GetGlobalNonRewindNetTime", "Script_GetGlobalNonRewindNetTime", "Gets a global non-rewind time", "float", "string name", false, Script_GetGlobalNonRewindNetTime);
    Script_RegisterFuncNamed(s, "SetGlobalNonRewindNetEnt", "Script_SetGlobalNonRewindNetEnt", "Sets a global non-rewind entity", "void", "string name, entity ent", false, Script_SetGlobalNonRewindNetEnt);
    Script_RegisterFuncNamed(s, "GetGlobalNonRewindNetEnt", "Script_GetGlobalNonRewindNetEnt", "Gets a global non-rewind entity", "entity ornull", "string name", false, Script_GetGlobalNonRewindNetEnt);


    // Indexed deathfield query functions
    DeathField_RegisterOnVM(s);

    // Deathfield configuration
    Script_RegisterFuncNamed(s, "DeathField_SetActive", "Script_DeathField_SetActive", "Sets a deathfield active state", "void", "int deathFieldIndex, bool active", false, Script_DeathField_SetActive);
    Script_RegisterFuncNamed(s, "DeathField_SetOrigin", "Script_DeathField_SetOrigin", "Sets a deathfield center origin", "void", "int deathFieldIndex, vector origin", false, Script_DeathField_SetOrigin);
    Script_RegisterFuncNamed(s, "DeathField_SetRadiusStartEnd", "Script_DeathField_SetRadiusStartEnd", "Sets deathfield radius start and end", "void", "int deathFieldIndex, float start, float end", false, Script_DeathField_SetRadiusStartEnd);
    Script_RegisterFuncNamed(s, "DeathField_SetTimeStartEnd", "Script_DeathField_SetTimeStartEnd", "Sets deathfield time start and end", "void", "int deathFieldIndex, float start, float end", false, Script_DeathField_SetTimeStartEnd);
}

//---------------------------------------------------------------------------------
// Purpose: admin server script functions
// Input  : *s - 
//---------------------------------------------------------------------------------
void Script_RegisterAdminServerFunctions(CSquirrelVM* s)
{
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, GetNumHumanPlayers, "Gets the number of human players on the server", "int", "", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, GetNumFakeClients, "Gets the number of bot players on the server", "int", "", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, CreateFakePlayer, "Creates a fake player and returns the edict index (-1 on failure). Use GetPlayerArray() to get entity.", "int", "string name, int team", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, KickPlayerByName, "Kicks a player from the server by name", "void", "string name, string reason", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, KickPlayerById, "Kicks a player from the server by handle or Steam ID", "void", "string id, string reason", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, BanPlayerByName, "Bans a player from the server by name", "void", "string name, string reason", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, BanPlayerById, "Bans a player from the server by handle or Steam ID", "void", "string id, string reason", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, UnbanPlayer, "Unbans a player from the server by Steam ID or ip address", "void", "string handle", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, BroadcastServerTextMessage, "Broadcasts a chatmessage to all clients", "void", "string prefix, string message, bool adminMsg", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, BroadcastChatBuilder, "Broadcasts a ChatBuilder message to all clients", "void", "string commands", false);
}

//---------------------------------------------------------------------------------
// Purpose: script code class function registration
//---------------------------------------------------------------------------------
static void Script_RegisterServerEntityClassFuncs()
{
    v_Script_RegisterServerEntityClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;

    WeaponScriptVars_RegisterEntityFuncs(g_serverScriptEntityStruct);
}
//---------------------------------------------------------------------------------
static void Script_RegisterServerPlayerClassFuncs()
{
    v_Script_RegisterServerPlayerClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;

    g_serverScriptPlayerStruct->AddFunction("SetClassVar",
        "ScriptSetClassVar",
        "Change a variable in the player's class settings",
        "bool",
        "string key, string value",
        false,
        ServerScript_ScriptSetClassVar);

    g_serverScriptPlayerStruct->AddFunction("SendServerTextMessage",
        "ScriptSendServerTextMessage",
        "Sends a chat message to a player",
        "bool",
        "string prefix, string message, bool adminMsg",
        false,
        ServerScript_SendServerTextMessage);

    g_serverScriptPlayerStruct->AddFunction("ChatBuilder",
        "ScriptChatBuilder",
        "Advanced chat builder API. Send commands as: 'N|' (newline), 'T|text|' (text), 'C|r,g,b|' (color), 'F|dur,fade|' (fade). Example: 'N|F|5,1|C|255,0,0|T|Red text!|'",
        "bool",
        "string commands",
        false,
        ServerScript_ChatBuilder);

    g_serverScriptPlayerStruct->AddFunction("ChatBuilderRainbow",
        "ScriptChatBuilderRainbow",
        "Sends a rainbow-colored message (cycles through colors per character). Keep text SHORT (under 20 chars recommended)",
        "bool",
        "string text, float duration, float fadeTime",
        false,
        ServerScript_ChatBuilderRainbow);

    // Register shared player functions (PushForcedStance, GetLastTimeDamaged, skydive, etc.)
    Script_RegisterPlayerScriptFunctions(g_serverScriptPlayerStruct);
    WeaponScriptVars_RegisterPhaseShiftOverride(g_serverScriptPlayerStruct);

    // Register SERVER-ONLY player setters (NonRewind setters must not be on CLIENT)
    Script_RegisterPlayerScriptSetters(g_serverScriptPlayerStruct);
}
//---------------------------------------------------------------------------------
static void Script_RegisterServerAIClassFuncs()
{
    v_Script_RegisterServerAIClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static SQRESULT ServerScript_SetScriptPoseParam0(HSQUIRRELVM v)
{
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT ServerScript_GetScriptPoseParam0(HSQUIRRELVM v)
{
    sq_pushfloat(v, 0.0f);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT ServerScript_SetScriptPoseParam1(HSQUIRRELVM v)
{
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT ServerScript_GetScriptPoseParam1(HSQUIRRELVM v)
{
    sq_pushfloat(v, 0.0f);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//---------------------------------------------------------------------------------
static void Script_RegisterServerWeaponClassFuncs()
{
    v_Script_RegisterServerWeaponClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;

    g_serverScriptWeaponStruct->AddFunction(
        "SetScriptPoseParam0",
        "Script_SetScriptPoseParam0",
        "Sets script pose parameter 0 (server no-op)",
        "void",
        "float value",
        false,
        ServerScript_SetScriptPoseParam0);

    g_serverScriptWeaponStruct->AddFunction(
        "GetScriptPoseParam0",
        "Script_GetScriptPoseParam0",
        "Gets script pose parameter 0 (server always returns 0.0)",
        "float",
        "",
        false,
        ServerScript_GetScriptPoseParam0);

    g_serverScriptWeaponStruct->AddFunction(
        "SetScriptPoseParam1",
        "Script_SetScriptPoseParam1",
        "Sets script pose parameter 1 (server no-op)",
        "void",
        "float value",
        false,
        ServerScript_SetScriptPoseParam1);

    g_serverScriptWeaponStruct->AddFunction(
        "GetScriptPoseParam1",
        "Script_GetScriptPoseParam1",
        "Gets script pose parameter 1 (server always returns 0.0)",
        "float",
        "",
        false,
        ServerScript_GetScriptPoseParam1);

    WeaponScriptVars_RegisterWeaponFuncs(g_serverScriptWeaponStruct);
    WeaponScriptVars_RegisterWeaponLockedSetSetter(g_serverScriptWeaponStruct);
    WeaponScriptVars_RegisterInfiniteAmmoFuncs(g_serverScriptWeaponStruct);
    WeaponScriptVars_RegisterInfiniteAmmoSetter(g_serverScriptWeaponStruct);
    WeaponHeat_RegisterWeaponFuncs(g_serverScriptWeaponStruct);
}
//---------------------------------------------------------------------------------
static void Script_RegisterServerProjectileClassFuncs()
{
    v_Script_RegisterServerProjectileClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterServerTitanSoulClassFuncs()
{
    v_Script_RegisterServerTitanSoulClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterServerPlayerDecoyClassFuncs()
{
    v_Script_RegisterServerPlayerDecoyClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterServerSpawnpointClassFuncs()
{
    v_Script_RegisterServerSpawnpointClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterServerFirstPersonProxyClassFuncs()
{
    v_Script_RegisterServerFirstPersonProxyClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}

void VScriptServer::Detour(const bool bAttach) const
{
    DetourSetup(&v_ServerScript_DebugScreenText, &ServerScript_DebugScreenText, bAttach);
    DetourSetup(&v_ServerScript_DebugScreenTextWithColor, &ServerScript_DebugScreenTextWithColor, bAttach);

    DetourSetup(&v_Script_RegisterServerEntityClassFuncs, &Script_RegisterServerEntityClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterServerPlayerClassFuncs, &Script_RegisterServerPlayerClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterServerAIClassFuncs, &Script_RegisterServerAIClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterServerWeaponClassFuncs, &Script_RegisterServerWeaponClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterServerProjectileClassFuncs, &Script_RegisterServerProjectileClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterServerTitanSoulClassFuncs, &Script_RegisterServerTitanSoulClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterServerPlayerDecoyClassFuncs, &Script_RegisterServerPlayerDecoyClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterServerSpawnpointClassFuncs, &Script_RegisterServerSpawnpointClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterServerFirstPersonProxyClassFuncs, &Script_RegisterServerFirstPersonProxyClassFuncs, bAttach);
}
