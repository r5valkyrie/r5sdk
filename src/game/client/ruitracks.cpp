//=============================================================================//
// Purpose: RUI_TRACK extended compatibility layer
// Registers custom track enums and provides value implementations via hook
//=============================================================================//

#include "core/stdafx.h"
#include "ruitracks.h"
#include "tier0/memaddr.h"
#include "vscript/languages/squirrel_re/vsquirrel.h"
#include "game/client/c_player.h"
#include "game/client/r1/c_weapon_x.h"
#include "game/client/cliententitylist.h"
#include "engine/client/clientstate.h"
#include "engine/client/vengineclient_impl.h"
#include "public/globalvars_base.h"
#include "game/shared/weapon_heat.h"
#include "public/inetchannel.h"
#include "game/shared/weapon_script_vars.h"
#include "game/client/vscript_player.h"

extern CGlobalVarsBase* gpGlobals;

//-----------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------
static constexpr float LATENCY_THRESHOLD_MS = 150.0f;
static constexpr float PACKETLOSS_THRESHOLD = 5.0f;
static constexpr uintptr_t RVA_TRACK_FALLBACK = 0x71DEC0;
static constexpr uintptr_t RVA_WORLD_ENTITY_PTR = 0x28160A78;

// Vanilla tracks max ID (IDs above this crash the original fallback handler)
static constexpr int VANILLA_TRACK_MAX = 97;

// Deathfield data offsets on C_World entity
static constexpr int OFF_DEATHFIELD_IS_ACTIVE = 0xA40;
static constexpr int OFF_DEATHFIELD_ORIGIN_X = 0xA44;
static constexpr int OFF_DEATHFIELD_ORIGIN_Y = 0xA48;
static constexpr int OFF_DEATHFIELD_RADIUS_START = 0xA50;
static constexpr int OFF_DEATHFIELD_RADIUS_END = 0xA54;
static constexpr int OFF_DEATHFIELD_TIME_START = 0xA58;
static constexpr int OFF_DEATHFIELD_TIME_END = 0xA5C;

// Player offset for minimap zoom
static constexpr int OFF_PLAYER_MINIMAP_ZOOM_SCALE = 0x4034;

// Weapon offsets for charge fraction
static constexpr int OFF_WEAPON_CHARGE_TIME = 0x1D34;
static constexpr int OFF_WEAPON_CHARGE_FRACTION = 0x1654;

enum CustomTrackId
{
    TRACK_DEATHFIELD_DISTANCE = 100,
    TRACK_MINIMAP_ZOOM_SCALE = 101,
    TRACK_WEAPON_CHARGE_FRACTION = 102,
    TRACK_POISE_FRACTION = 103,
    TRACK_WEAPON_LAST_PRIMARY_ATTACK_TIME = 104,
    TRACK_PLAYER_IS_DRIVING_HOVER_VEHICLE = 105,
	
    TRACK_NETGRAPH_FPS = 106,
    TRACK_NETGRAPH_SPING = 107,
    TRACK_NETGRAPH_PACKETLOSS = 108,
	
    TRACK_NET_ISSUE_CONGESTION = 109,
    TRACK_NET_ISSUE_LATENCY = 110,
    TRACK_NET_ISSUE_PACKETLOSS = 111,
	
    TRACK_NETGRAPH_BANDWIDTH_IN = 112,
    TRACK_NETGRAPH_BANDWIDTH_OUT = 113,
    TRACK_NETGRAPH_PACKETCHOKE = 114,
	
    TRACK_NET_ISSUE_PREDICTION_ERROR = 115,
    TRACK_WEAPON_CHARGE_FRACTION_CURVED = 116,
    TRACK_WEAPON_SCRIPT_FLOAT_0 = 117,
    TRACK_GRENADE_DIST_FROM_IMPACT = 118,
	
    TRACK_NETGRAPH_PINGVAR = 119,
    TRACK_NETGRAPH_REMOTEFPS = 120,
    TRACK_NETGRAPH_USRCMD = 121,
	
    TRACK_VIEWCONE_MINPITCH = 122,
    TRACK_VIEWCONE_MAXPITCH = 123,
    TRACK_VIEWCONE_MINYAW = 124,
    TRACK_VIEWCONE_MAXYAW = 125,
	
    TRACK_AUDIO_ISSUE_DATA = 126,
    TRACK_AUDIO_ISSUE_LIMITS = 127,
    TRACK_AUDIO_ISSUE_MARKER_INSERTED = 128,
    TRACK_AUDIO_ISSUE_STARVATION = 129,
	
    TRACK_NETGRAPH_PACKETLOSS_IN = 130,
    TRACK_NETGRAPH_PACKETLOSS_OUT = 131,
    TRACK_NETGRAPH_PACKETCHOKE_IN = 132,
    TRACK_NETGRAPH_PACKETCHOKE_OUT = 133,
    TRACK_NETGRAPH_DATABLOCK_SIZE = 134,
    TRACK_NETGRAPH_DATABLOCK_PERCENT_DONE = 135,

    TRACK_EXTRA_SHIELD_INT = 136,
    TRACK_EXTRA_SHIELD_TIER_INT = 137,

    TRACK_CUSTOM_MIN = 100,
    TRACK_CUSTOM_MAX = 137
};

//-----------------------------------------------------------------------------
// State
//-----------------------------------------------------------------------------
inline __int64(*v_RuiTrack_Fallback)(__int64, int, unsigned int, int, int, int, int*);
static bool s_bInitialized = false;
static uintptr_t s_moduleBase = 0;
static void** s_ppWorldEntity = nullptr;

//-----------------------------------------------------------------------------
// Track enum definitions for script registration
//-----------------------------------------------------------------------------
struct TrackDef { const char* name; int id; };

static const TrackDef s_customTracks[] = {
    { "RUI_TRACK_DEATHFIELD_DISTANCE", TRACK_DEATHFIELD_DISTANCE },
    { "RUI_TRACK_MINIMAP_ZOOM_SCALE", TRACK_MINIMAP_ZOOM_SCALE },
    { "RUI_TRACK_WEAPON_CHARGE_FRACTION", TRACK_WEAPON_CHARGE_FRACTION },
    { "RUI_TRACK_POISE_FRACTION", TRACK_POISE_FRACTION },
    { "RUI_TRACK_WEAPON_LAST_PRIMARY_ATTACK_TIME", TRACK_WEAPON_LAST_PRIMARY_ATTACK_TIME },
    { "RUI_TRACK_PLAYER_IS_DRIVING_HOVER_VEHICLE", TRACK_PLAYER_IS_DRIVING_HOVER_VEHICLE },
    { "RUI_TRACK_NETGRAPH_FPS", TRACK_NETGRAPH_FPS },
    { "RUI_TRACK_NETGRAPH_SPING", TRACK_NETGRAPH_SPING },
    { "RUI_TRACK_NETGRAPH_PACKETLOSS", TRACK_NETGRAPH_PACKETLOSS },
    { "RUI_TRACK_NET_ISSUE_CONGESTION", TRACK_NET_ISSUE_CONGESTION },
    { "RUI_TRACK_NET_ISSUE_LATENCY", TRACK_NET_ISSUE_LATENCY },
    { "RUI_TRACK_NET_ISSUE_PACKETLOSS", TRACK_NET_ISSUE_PACKETLOSS },
    { "RUI_TRACK_NET_ISSUE_PREDICTION_ERROR", TRACK_NET_ISSUE_PREDICTION_ERROR },
    { "RUI_TRACK_NETGRAPH_BANDWIDTH_IN", TRACK_NETGRAPH_BANDWIDTH_IN },
    { "RUI_TRACK_NETGRAPH_BANDWIDTH_OUT", TRACK_NETGRAPH_BANDWIDTH_OUT },
    { "RUI_TRACK_NETGRAPH_PACKETCHOKE", TRACK_NETGRAPH_PACKETCHOKE },
    { "RUI_TRACK_WEAPON_CHARGE_FRACTION_CURVED", TRACK_WEAPON_CHARGE_FRACTION_CURVED },
    { "RUI_TRACK_WEAPON_SCRIPT_FLOAT_0", TRACK_WEAPON_SCRIPT_FLOAT_0 },
    { "RUI_TRACK_GRENADE_DIST_FROM_IMPACT", TRACK_GRENADE_DIST_FROM_IMPACT },
    { "RUI_TRACK_NETGRAPH_PINGVAR", TRACK_NETGRAPH_PINGVAR },
    { "RUI_TRACK_NETGRAPH_REMOTEFPS", TRACK_NETGRAPH_REMOTEFPS },
    { "RUI_TRACK_NETGRAPH_USRCMD", TRACK_NETGRAPH_USRCMD },
    { "RUI_TRACK_VIEWCONE_MINPITCH", TRACK_VIEWCONE_MINPITCH },
    { "RUI_TRACK_VIEWCONE_MAXPITCH", TRACK_VIEWCONE_MAXPITCH },
    { "RUI_TRACK_VIEWCONE_MINYAW", TRACK_VIEWCONE_MINYAW },
    { "RUI_TRACK_VIEWCONE_MAXYAW", TRACK_VIEWCONE_MAXYAW },
    { "RUI_TRACK_AUDIO_ISSUE_DATA", TRACK_AUDIO_ISSUE_DATA },
    { "RUI_TRACK_AUDIO_ISSUE_LIMITS", TRACK_AUDIO_ISSUE_LIMITS },
    { "RUI_TRACK_AUDIO_ISSUE_MARKER_INSERTED", TRACK_AUDIO_ISSUE_MARKER_INSERTED },
    { "RUI_TRACK_AUDIO_ISSUE_STARVATION", TRACK_AUDIO_ISSUE_STARVATION },
    { "RUI_TRACK_NETGRAPH_PACKETLOSS_IN", TRACK_NETGRAPH_PACKETLOSS_IN },
    { "RUI_TRACK_NETGRAPH_PACKETLOSS_OUT", TRACK_NETGRAPH_PACKETLOSS_OUT },
    { "RUI_TRACK_NETGRAPH_PACKETCHOKE_IN", TRACK_NETGRAPH_PACKETCHOKE_IN },
    { "RUI_TRACK_NETGRAPH_PACKETCHOKE_OUT", TRACK_NETGRAPH_PACKETCHOKE_OUT },
    { "RUI_TRACK_NETGRAPH_DATABLOCK_SIZE", TRACK_NETGRAPH_DATABLOCK_SIZE },
    { "RUI_TRACK_NETGRAPH_DATABLOCK_PERCENT_DONE", TRACK_NETGRAPH_DATABLOCK_PERCENT_DONE },
    { "RUI_TRACK_EXTRA_SHIELD_INT", TRACK_EXTRA_SHIELD_INT },
    { "RUI_TRACK_EXTRA_SHIELD_TIER_INT", TRACK_EXTRA_SHIELD_TIER_INT },
    { nullptr, 0 }
};

//-----------------------------------------------------------------------------
// Pointer validation helpers
//-----------------------------------------------------------------------------
static bool IsValidReadPointer(const void* ptr, size_t size)
{
    if (!ptr) return false;
    if (reinterpret_cast<uintptr_t>(ptr) < 0x10000) return false;
    if (reinterpret_cast<uintptr_t>(ptr) & 0x3) return false;

    __try {
        volatile char test = *static_cast<const volatile char*>(ptr);
        (void)test;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool IsValidWritePointer(void* ptr, size_t size)
{
    if (!ptr) return false;
    if (reinterpret_cast<uintptr_t>(ptr) < 0x10000) return false;

    __try {
        volatile char* test = static_cast<volatile char*>(ptr);
        char tmp = *test;
        *test = tmp;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static CNetChan* RuiTracks_GetNetChannel()
{
    CClientState* pClient = g_pClientState;
    if (!pClient) return nullptr;

    CNetChan* pChan = pClient->m_NetChannel;
    if (!pChan || !IsValidReadPointer(pChan, sizeof(void*))) return nullptr;

    return pChan;
}

//-----------------------------------------------------------------------------
// Float track getters
//-----------------------------------------------------------------------------
static float GetTrack_DeathfieldDistance()
{
    if (!s_ppWorldEntity) return 10000.0f;

    // Capture world pointer once to avoid TOCTOU race
    void* pWorld = *s_ppWorldEntity;
    if (!pWorld || !IsValidReadPointer(pWorld, OFF_DEATHFIELD_TIME_END + sizeof(float))) return 10000.0f;
    if (!g_pEngineClient || !g_pClientEntityList) return 10000.0f;

    // Check if deathfield is active
    bool isActive = *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(pWorld) + OFF_DEATHFIELD_IS_ACTIVE);
    if (!isActive) return 10000.0f;

    // Get local player position
    int localIdx = g_pEngineClient->GetLocalPlayer();
    if (localIdx <= 0) return 10000.0f;

    IClientEntity* pLocal = g_pClientEntityList->GetClientEntity(localIdx);
    if (!pLocal || !IsValidReadPointer(pLocal, 0x160)) return 10000.0f;

    // Player position is typically at offset 0x150 (m_vecAbsOrigin)
    float playerX = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pLocal) + 0x150);
    float playerY = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pLocal) + 0x154);

    // Deathfield origin
    float originX = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pWorld) + OFF_DEATHFIELD_ORIGIN_X);
    float originY = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pWorld) + OFF_DEATHFIELD_ORIGIN_Y);

    // Calculate distance from ring center
    float dx = playerX - originX;
    float dy = playerY - originY;
    float distFromCenter = sqrtf(dx * dx + dy * dy);

    // Get current radius (interpolated between start and end based on time)
    float radiusStart = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pWorld) + OFF_DEATHFIELD_RADIUS_START);
    float radiusEnd = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pWorld) + OFF_DEATHFIELD_RADIUS_END);
    float timeStart = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pWorld) + OFF_DEATHFIELD_TIME_START);
    float timeEnd = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pWorld) + OFF_DEATHFIELD_TIME_END);

    float currentRadius;
    if (timeEnd == timeStart)
    {
        currentRadius = (gpGlobals && gpGlobals->curTime >= timeEnd) ? radiusEnd : radiusStart;
    }
    else
    {
        float t = (gpGlobals ? gpGlobals->curTime : 0.0f) - timeStart;
        float fraction = t / (timeEnd - timeStart);
        fraction = fmaxf(0.0f, fminf(1.0f, fraction));
        currentRadius = radiusStart + (radiusEnd - radiusStart) * fraction;
    }

    // Distance from frontier (positive = inside ring, negative = outside)
    return currentRadius - distFromCenter;
}

static float GetTrack_MinimapZoomScale()
{
    if (!g_pEngineClient || !g_pClientEntityList) return 1.0f;

    int localIdx = g_pEngineClient->GetLocalPlayer();
    if (localIdx <= 0) return 1.0f;

    IClientEntity* pLocal = g_pClientEntityList->GetClientEntity(localIdx);
    if (!pLocal || !IsValidReadPointer(pLocal, OFF_PLAYER_MINIMAP_ZOOM_SCALE + sizeof(float))) return 1.0f;

    float zoom = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pLocal) + OFF_PLAYER_MINIMAP_ZOOM_SCALE);
    return (zoom > 0.0f) ? zoom : 1.0f;
}

static float GetTrack_WeaponChargeFraction(__int64 entity)
{
    C_BaseCombatCharacter* pOwner = nullptr;

    if (entity)
    {
        void* entityPtr = reinterpret_cast<void*>(entity);
        if (!IsValidReadPointer(entityPtr, sizeof(void*))) return 0.0f;
        pOwner = reinterpret_cast<C_BaseCombatCharacter*>(entity);
    }
    else
    {
        if (!g_pEngineClient || !g_pClientEntityList) return 0.0f;

        int localIdx = g_pEngineClient->GetLocalPlayer();
        if (localIdx <= 0) return 0.0f;

        IClientEntity* pLocal = g_pClientEntityList->GetClientEntity(localIdx);
        if (!pLocal || !IsValidReadPointer(pLocal, sizeof(void*))) return 0.0f;

        pOwner = reinterpret_cast<C_BaseCombatCharacter*>(pLocal);
    }

    C_WeaponX* weapon = C_BaseCombatCharacter__GetActiveWeapon(pOwner);
    if (!weapon || !IsValidReadPointer(weapon, OFF_WEAPON_CHARGE_TIME + sizeof(float))) return 0.0f;

    // Check if weapon has charge capability (charge_time > 0)
    float chargeTime = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(weapon) + OFF_WEAPON_CHARGE_TIME);
    if (chargeTime <= 0.0f) return 0.0f;

    float fraction = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(weapon) + OFF_WEAPON_CHARGE_FRACTION);
    return fmaxf(0.0f, fminf(1.0f, fraction));
}

static float GetTrack_WeaponLastAttackTime(__int64 entity)
{
    C_WeaponX* weapon = nullptr;

    if (entity)
    {
        // Entity is the weapon itself (script passes weapon directly)
        void* entityPtr = reinterpret_cast<void*>(entity);
        if (!IsValidReadPointer(entityPtr, sizeof(C_WeaponX))) return 0.0f;
        weapon = reinterpret_cast<C_WeaponX*>(entity);
    }
    else
    {
        // Entity is null - get local player's active weapon
        if (!g_pEngineClient || !g_pClientEntityList) return 0.0f;

        int localIdx = g_pEngineClient->GetLocalPlayer();
        if (localIdx <= 0) return 0.0f;

        IClientEntity* pLocal = g_pClientEntityList->GetClientEntity(localIdx);
        if (!pLocal || !IsValidReadPointer(pLocal, sizeof(void*))) return 0.0f;

        C_BaseCombatCharacter* pOwner = reinterpret_cast<C_BaseCombatCharacter*>(pLocal);
        weapon = C_BaseCombatCharacter__GetActiveWeapon(pOwner);
    }

    if (!weapon || !IsValidReadPointer(weapon, sizeof(C_WeaponX))) return 0.0f;

    return weapon->GetLastPrimaryAttack();
}

static float GetTrack_WeaponScriptFloat0(__int64 entity)
{
    C_WeaponX* weapon = nullptr;

    if (entity)
    {
        void* entityPtr = reinterpret_cast<void*>(entity);
        if (!IsValidReadPointer(entityPtr, sizeof(void*))) return 0.0f;
        weapon = reinterpret_cast<C_WeaponX*>(entity);
    }
    else
    {
        if (!g_pEngineClient || !g_pClientEntityList) return 0.0f;

        int localIdx = g_pEngineClient->GetLocalPlayer();
        if (localIdx <= 0) return 0.0f;

        IClientEntity* pLocal = g_pClientEntityList->GetClientEntity(localIdx);
        if (!pLocal || !IsValidReadPointer(pLocal, sizeof(void*))) return 0.0f;

        C_BaseCombatCharacter* pOwner = reinterpret_cast<C_BaseCombatCharacter*>(pLocal);
        weapon = C_BaseCombatCharacter__GetActiveWeapon(pOwner);
    }

    if (!weapon || !IsValidReadPointer(weapon, sizeof(C_WeaponX))) return 0.0f;

    return WeaponScriptVars_GetScriptFloat0(weapon);
}

//-----------------------------------------------------------------------------
// Int track getters
//-----------------------------------------------------------------------------
static int GetTrack_FPS()
{
    if (!gpGlobals || gpGlobals->frameTime <= 0.0f) return 0;

    int fps = static_cast<int>(1.0f / gpGlobals->frameTime);
    return (fps < 0) ? 0 : ((fps > 9999) ? 9999 : fps);
}

static int GetTrack_Ping()
{
    CNetChan* pChan = RuiTracks_GetNetChannel();
    if (!pChan) return 0;

    int ping = static_cast<int>(pChan->GetAvgLatency(FLOW_OUTGOING) * 1000.0f + 0.5f);
    return (ping < 0) ? 0 : ((ping > 9999) ? 9999 : ping);
}

static int GetTrack_PacketLoss()
{
    CNetChan* pChan = RuiTracks_GetNetChannel();
    if (!pChan) return 0;

    int loss = static_cast<int>(pChan->GetAvgLoss(FLOW_INCOMING) * 100.0f + 0.5f);
    return (loss < 0) ? 0 : ((loss > 100) ? 100 : loss);
}

static int GetTrack_BandwidthIn()
{
    CNetChan* pChan = RuiTracks_GetNetChannel();
    if (!pChan) return 0;

    int bw = static_cast<int>(pChan->GetAvgData(FLOW_INCOMING) + 0.5f);
    return (bw < 0) ? 0 : bw;
}

static int GetTrack_BandwidthOut()
{
    CNetChan* pChan = RuiTracks_GetNetChannel();
    if (!pChan) return 0;

    int bw = static_cast<int>(pChan->GetAvgData(FLOW_OUTGOING) + 0.5f);
    return (bw < 0) ? 0 : bw;
}

static int GetTrack_PacketChoke()
{
    CNetChan* pChan = RuiTracks_GetNetChannel();
    if (!pChan) return 0;

    int choke = static_cast<int>(pChan->GetAvgChoke(FLOW_INCOMING) * 100.0f + 0.5f);
    return (choke < 0) ? 0 : ((choke > 100) ? 100 : choke);
}

static int GetTrack_PingVar()
{
    CNetChan* pChan = RuiTracks_GetNetChannel();
    if (!pChan) return 0;

    // Approximate jitter as absolute difference between current and average latency
    float avgMs = pChan->GetAvgLatency(FLOW_OUTGOING) * 1000.0f;
    float curMs = pChan->GetLatency(FLOW_OUTGOING) * 1000.0f;
    int variance = static_cast<int>(fabsf(curMs - avgMs) + 0.5f);
    return (variance < 0) ? 0 : ((variance > 9999) ? 9999 : variance);
}

static int GetTrack_PacketLossIn()
{
    CNetChan* pChan = RuiTracks_GetNetChannel();
    if (!pChan) return 0;

    int loss = static_cast<int>(pChan->GetAvgLoss(FLOW_INCOMING) * 100.0f + 0.5f);
    return (loss < 0) ? 0 : ((loss > 100) ? 100 : loss);
}

static int GetTrack_PacketLossOut()
{
    CNetChan* pChan = RuiTracks_GetNetChannel();
    if (!pChan) return 0;

    int loss = static_cast<int>(pChan->GetAvgLoss(FLOW_OUTGOING) * 100.0f + 0.5f);
    return (loss < 0) ? 0 : ((loss > 100) ? 100 : loss);
}

static int GetTrack_PacketChokeIn()
{
    CNetChan* pChan = RuiTracks_GetNetChannel();
    if (!pChan) return 0;

    int choke = static_cast<int>(pChan->GetAvgChoke(FLOW_INCOMING) * 100.0f + 0.5f);
    return (choke < 0) ? 0 : ((choke > 100) ? 100 : choke);
}

static int GetTrack_PacketChokeOut()
{
    CNetChan* pChan = RuiTracks_GetNetChannel();
    if (!pChan) return 0;

    int choke = static_cast<int>(pChan->GetAvgChoke(FLOW_OUTGOING) * 100.0f + 0.5f);
    return (choke < 0) ? 0 : ((choke > 100) ? 100 : choke);
}

//-----------------------------------------------------------------------------
// Bool track getters
//-----------------------------------------------------------------------------
static bool GetTrack_HighLatency()
{
    CNetChan* pChan = RuiTracks_GetNetChannel();
    if (!pChan) return false;
    return (pChan->GetAvgLatency(FLOW_INCOMING) * 1000.0f) > LATENCY_THRESHOLD_MS;
}

static bool GetTrack_PacketLossIssue()
{
    CNetChan* pChan = RuiTracks_GetNetChannel();
    if (!pChan) return false;
    return (pChan->GetAvgLoss(FLOW_INCOMING) * 100.0f) > PACKETLOSS_THRESHOLD;
}

//-----------------------------------------------------------------------------
// Fallback hook - handles custom track IDs
//-----------------------------------------------------------------------------
static __int64 RuiTrack_Fallback_Hook(__int64 entity, int trackId, unsigned int customIndex,
    int a4, int a5, int a6, int* output)
{
    if (!output || !IsValidWritePointer(output, sizeof(int))) return 0;

    if (trackId >= TRACK_CUSTOM_MIN && trackId <= TRACK_CUSTOM_MAX)
    {
        switch (trackId)
        {
            case TRACK_DEATHFIELD_DISTANCE:
                *(float*)output = GetTrack_DeathfieldDistance();
                return 0;
            case TRACK_MINIMAP_ZOOM_SCALE:
                *(float*)output = GetTrack_MinimapZoomScale();
                return 0;
            case TRACK_WEAPON_CHARGE_FRACTION:
                *(float*)output = GetTrack_WeaponChargeFraction(entity);
                return 0;
            case TRACK_POISE_FRACTION:
                *(float*)output = 1.0f;
                return 0;
            case TRACK_WEAPON_LAST_PRIMARY_ATTACK_TIME:
                *(float*)output = GetTrack_WeaponLastAttackTime(entity);
                return 0;
            case TRACK_NETGRAPH_FPS:
                *output = GetTrack_FPS();
                return 0;
            case TRACK_NETGRAPH_SPING:
                *output = GetTrack_Ping();
                return 0;
            case TRACK_NETGRAPH_PACKETLOSS:
                *output = GetTrack_PacketLoss();
                return 0;
            case TRACK_NETGRAPH_BANDWIDTH_IN:
                *output = GetTrack_BandwidthIn();
                return 0;
            case TRACK_NETGRAPH_BANDWIDTH_OUT:
                *output = GetTrack_BandwidthOut();
                return 0;
            case TRACK_NETGRAPH_PACKETCHOKE:
                *output = GetTrack_PacketChoke();
                return 0;

            // Extended float tracks
            case TRACK_WEAPON_CHARGE_FRACTION_CURVED:
            {
                float curved = WeaponHeat_GetCurvedChargeFraction(reinterpret_cast<void*>(entity));
                if (curved > 0.0f)
                    *(float*)output = curved;
                else
                    *(float*)output = GetTrack_WeaponChargeFraction(entity);
                return 0;
            }
            case TRACK_WEAPON_SCRIPT_FLOAT_0:
                *(float*)output = GetTrack_WeaponScriptFloat0(entity);
                return 0;
            case TRACK_GRENADE_DIST_FROM_IMPACT:
                *(float*)output = 0.0f;
                return 0;

            // Extended int tracks
            case TRACK_NETGRAPH_PINGVAR:
                *output = GetTrack_PingVar();
                return 0;
            case TRACK_NETGRAPH_REMOTEFPS:
            case TRACK_NETGRAPH_USRCMD:
                *output = 0;
                return 0;

            // Viewcone tracks (full range = no culling, safe defaults)
            case TRACK_VIEWCONE_MINPITCH:
                *(float*)output = -90.0f;
                return 0;
            case TRACK_VIEWCONE_MAXPITCH:
                *(float*)output = 90.0f;
                return 0;
            case TRACK_VIEWCONE_MINYAW:
                *(float*)output = -180.0f;
                return 0;
            case TRACK_VIEWCONE_MAXYAW:
                *(float*)output = 180.0f;
                return 0;

            // Audio issue tracks (no issues = 0)
            case TRACK_AUDIO_ISSUE_DATA:
            case TRACK_AUDIO_ISSUE_LIMITS:
            case TRACK_AUDIO_ISSUE_MARKER_INSERTED:
            case TRACK_AUDIO_ISSUE_STARVATION:
                *reinterpret_cast<unsigned char*>(output) = 0;
                return 0;

            // S12 directional netgraph tracks
            case TRACK_NETGRAPH_PACKETLOSS_IN:
                *output = GetTrack_PacketLossIn();
                return 0;
            case TRACK_NETGRAPH_PACKETLOSS_OUT:
                *output = GetTrack_PacketLossOut();
                return 0;
            case TRACK_NETGRAPH_PACKETCHOKE_IN:
                *output = GetTrack_PacketChokeIn();
                return 0;
            case TRACK_NETGRAPH_PACKETCHOKE_OUT:
                *output = GetTrack_PacketChokeOut();
                return 0;

            // S12 datablock transfer tracks
            case TRACK_NETGRAPH_DATABLOCK_SIZE:
                *output = 0;
                return 0;
            case TRACK_NETGRAPH_DATABLOCK_PERCENT_DONE:
                *(float*)output = 0.0f;
                return 0;

            // Bool tracks
            case TRACK_PLAYER_IS_DRIVING_HOVER_VEHICLE:
            case TRACK_NET_ISSUE_CONGESTION:
            case TRACK_NET_ISSUE_PREDICTION_ERROR:
                *reinterpret_cast<unsigned char*>(output) = 0;
                return 0;
            case TRACK_NET_ISSUE_LATENCY:
                *reinterpret_cast<unsigned char*>(output) = GetTrack_HighLatency() ? 1 : 0;
                return 0;
            case TRACK_NET_ISSUE_PACKETLOSS:
                *reinterpret_cast<unsigned char*>(output) = GetTrack_PacketLossIssue() ? 1 : 0;
                return 0;

            case TRACK_EXTRA_SHIELD_INT:
                *output = entity ? VScriptPlayer_GetExtraShieldHealth(reinterpret_cast<void*>(entity)) : 0;
                return 0;
            case TRACK_EXTRA_SHIELD_TIER_INT:
                *output = entity ? VScriptPlayer_GetExtraShieldTier(reinterpret_cast<void*>(entity)) : 0;
                return 0;

            default:
                *output = 0;
                return 0;
        }
    }

    if (trackId > VANILLA_TRACK_MAX)
    {
        *output = 0;
        return 0;
    }

    return v_RuiTrack_Fallback(entity, trackId, customIndex, a4, a5, a6, output);
}

//-----------------------------------------------------------------------------
// Public: Register missing enums to script VM
//-----------------------------------------------------------------------------
void RuiTracks_RegisterMissingEnums(CSquirrelVM* const s)
{
    if (!s) return;

    for (const TrackDef* def = s_customTracks; def->name != nullptr; def++)
        s->RegisterConstant(def->name, def->id);
}

//-----------------------------------------------------------------------------
// Initialization
//-----------------------------------------------------------------------------
void RuiTracks_Init()
{
    if (s_bInitialized) return;

    s_moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("r5apex.exe"));
    if (!s_moduleBase)
    {
        Warning(eDLL_T::CLIENT, "RuiTracks: Failed to get module base\n");
        return;
    }

    v_RuiTrack_Fallback = reinterpret_cast<decltype(v_RuiTrack_Fallback)>(
        s_moduleBase + RVA_TRACK_FALLBACK);

    s_ppWorldEntity = reinterpret_cast<void**>(s_moduleBase + RVA_WORLD_ENTITY_PTR);

    s_bInitialized = true;
}

//-----------------------------------------------------------------------------
// Debug command
//-----------------------------------------------------------------------------
static void CC_RuiTrackDebug_f(const CCommand& args)
{
    Msg(eDLL_T::CLIENT, "===============================================\n");
    Msg(eDLL_T::CLIENT, "         RUI Custom Track Debug Output\n");
    Msg(eDLL_T::CLIENT, "===============================================\n\n");

    // Float tracks
    Msg(eDLL_T::CLIENT, "-- Float Tracks --\n");
    Msg(eDLL_T::CLIENT, "  [%3d] DEATHFIELD_DISTANCE           = %12.4f %s\n",
        TRACK_DEATHFIELD_DISTANCE, GetTrack_DeathfieldDistance(),
        "(positive=inside ring)");
    Msg(eDLL_T::CLIENT, "  [%3d] MINIMAP_ZOOM_SCALE            = %12.4f %s\n",
        TRACK_MINIMAP_ZOOM_SCALE, GetTrack_MinimapZoomScale(),
        "(1.0=default)");
    Msg(eDLL_T::CLIENT, "  [%3d] WEAPON_CHARGE_FRACTION        = %12.4f %s\n",
        TRACK_WEAPON_CHARGE_FRACTION, GetTrack_WeaponChargeFraction(0),
        "(0.0-1.0)");
    Msg(eDLL_T::CLIENT, "  [%3d] POISE_FRACTION                = %12.4f %s\n",
        TRACK_POISE_FRACTION, 1.0f,
        "(STUB)");
    Msg(eDLL_T::CLIENT, "  [%3d] WEAPON_LAST_PRIMARY_ATTACK    = %12.4f %s\n",
        TRACK_WEAPON_LAST_PRIMARY_ATTACK_TIME, GetTrack_WeaponLastAttackTime(0),
        "(game time)");
    Msg(eDLL_T::CLIENT, "  [%3d] WEAPON_CHARGE_FRACTION_CURVED = %12.4f %s\n",
        TRACK_WEAPON_CHARGE_FRACTION_CURVED, WeaponHeat_GetCurvedChargeFraction(nullptr),
        "(cubic curve)");
    Msg(eDLL_T::CLIENT, "  [%3d] WEAPON_SCRIPT_FLOAT_0         = %12.4f %s\n",
        TRACK_WEAPON_SCRIPT_FLOAT_0, GetTrack_WeaponScriptFloat0(0),
        "(from WeaponScriptVars)");
    Msg(eDLL_T::CLIENT, "  [%3d] GRENADE_DIST_FROM_IMPACT      = %12.4f %s\n",
        TRACK_GRENADE_DIST_FROM_IMPACT, 0.0f, "(STUB)");
    Msg(eDLL_T::CLIENT, "  [%3d] VIEWCONE_MINPITCH             = %12.4f\n",
        TRACK_VIEWCONE_MINPITCH, -90.0f);
    Msg(eDLL_T::CLIENT, "  [%3d] VIEWCONE_MAXPITCH             = %12.4f\n",
        TRACK_VIEWCONE_MAXPITCH, 90.0f);
    Msg(eDLL_T::CLIENT, "  [%3d] VIEWCONE_MINYAW               = %12.4f\n",
        TRACK_VIEWCONE_MINYAW, -180.0f);
    Msg(eDLL_T::CLIENT, "  [%3d] VIEWCONE_MAXYAW               = %12.4f\n",
        TRACK_VIEWCONE_MAXYAW, 180.0f);
    Msg(eDLL_T::CLIENT, "  [%3d] DATABLOCK_PERCENT_DONE        = %12.4f %s\n",
        TRACK_NETGRAPH_DATABLOCK_PERCENT_DONE, 0.0f, "(STUB)");

    Msg(eDLL_T::CLIENT, "\n-- Int Tracks --\n");
    Msg(eDLL_T::CLIENT, "  [%3d] NETGRAPH_FPS                  = %12d\n",
        TRACK_NETGRAPH_FPS, GetTrack_FPS());
    Msg(eDLL_T::CLIENT, "  [%3d] NETGRAPH_SPING                = %12d ms\n",
        TRACK_NETGRAPH_SPING, GetTrack_Ping());
    Msg(eDLL_T::CLIENT, "  [%3d] NETGRAPH_PINGVAR              = %12d ms\n",
        TRACK_NETGRAPH_PINGVAR, GetTrack_PingVar());
    Msg(eDLL_T::CLIENT, "  [%3d] NETGRAPH_PACKETLOSS           = %12d %%\n",
        TRACK_NETGRAPH_PACKETLOSS, GetTrack_PacketLoss());
    Msg(eDLL_T::CLIENT, "  [%3d] NETGRAPH_PACKETLOSS_IN        = %12d %%\n",
        TRACK_NETGRAPH_PACKETLOSS_IN, GetTrack_PacketLossIn());
    Msg(eDLL_T::CLIENT, "  [%3d] NETGRAPH_PACKETLOSS_OUT       = %12d %%\n",
        TRACK_NETGRAPH_PACKETLOSS_OUT, GetTrack_PacketLossOut());
    Msg(eDLL_T::CLIENT, "  [%3d] NETGRAPH_BANDWIDTH_IN         = %12d bytes/s\n",
        TRACK_NETGRAPH_BANDWIDTH_IN, GetTrack_BandwidthIn());
    Msg(eDLL_T::CLIENT, "  [%3d] NETGRAPH_BANDWIDTH_OUT        = %12d bytes/s\n",
        TRACK_NETGRAPH_BANDWIDTH_OUT, GetTrack_BandwidthOut());
    Msg(eDLL_T::CLIENT, "  [%3d] NETGRAPH_PACKETCHOKE          = %12d %%\n",
        TRACK_NETGRAPH_PACKETCHOKE, GetTrack_PacketChoke());
    Msg(eDLL_T::CLIENT, "  [%3d] NETGRAPH_PACKETCHOKE_IN       = %12d %%\n",
        TRACK_NETGRAPH_PACKETCHOKE_IN, GetTrack_PacketChokeIn());
    Msg(eDLL_T::CLIENT, "  [%3d] NETGRAPH_PACKETCHOKE_OUT      = %12d %%\n",
        TRACK_NETGRAPH_PACKETCHOKE_OUT, GetTrack_PacketChokeOut());
    Msg(eDLL_T::CLIENT, "  [%3d] NETGRAPH_REMOTEFPS            = %12d %s\n",
        TRACK_NETGRAPH_REMOTEFPS, 0, "(STUB)");
    Msg(eDLL_T::CLIENT, "  [%3d] NETGRAPH_USRCMD               = %12d %s\n",
        TRACK_NETGRAPH_USRCMD, 0, "(STUB)");
    Msg(eDLL_T::CLIENT, "  [%3d] NETGRAPH_DATABLOCK_SIZE       = %12d %s\n",
        TRACK_NETGRAPH_DATABLOCK_SIZE, 0, "(STUB)");

    Msg(eDLL_T::CLIENT, "\n-- Bool Tracks --\n");
    Msg(eDLL_T::CLIENT, "  [%3d] PLAYER_IS_DRIVING_HOVER       = %12s %s\n",
        TRACK_PLAYER_IS_DRIVING_HOVER_VEHICLE, "false", "(STUB)");
    Msg(eDLL_T::CLIENT, "  [%3d] NET_ISSUE_CONGESTION          = %12s %s\n",
        TRACK_NET_ISSUE_CONGESTION, "false", "(STUB)");
    Msg(eDLL_T::CLIENT, "  [%3d] NET_ISSUE_LATENCY             = %12s %s\n",
        TRACK_NET_ISSUE_LATENCY, GetTrack_HighLatency() ? "true" : "false",
        "(>150ms)");
    Msg(eDLL_T::CLIENT, "  [%3d] NET_ISSUE_PACKETLOSS          = %12s %s\n",
        TRACK_NET_ISSUE_PACKETLOSS, GetTrack_PacketLossIssue() ? "true" : "false",
        "(>5%)");
    Msg(eDLL_T::CLIENT, "  [%3d] NET_ISSUE_PREDICTION_ERROR    = %12s %s\n",
        TRACK_NET_ISSUE_PREDICTION_ERROR, "false", "(STUB)");
    Msg(eDLL_T::CLIENT, "  [%3d] AUDIO_ISSUE_DATA              = %12s %s\n",
        TRACK_AUDIO_ISSUE_DATA, "false", "(STUB)");
    Msg(eDLL_T::CLIENT, "  [%3d] AUDIO_ISSUE_LIMITS            = %12s %s\n",
        TRACK_AUDIO_ISSUE_LIMITS, "false", "(STUB)");
    Msg(eDLL_T::CLIENT, "  [%3d] AUDIO_ISSUE_MARKER_INSERTED   = %12s %s\n",
        TRACK_AUDIO_ISSUE_MARKER_INSERTED, "false", "(STUB)");
    Msg(eDLL_T::CLIENT, "  [%3d] AUDIO_ISSUE_STARVATION        = %12s %s\n",
        TRACK_AUDIO_ISSUE_STARVATION, "false", "(STUB)");

    Msg(eDLL_T::CLIENT, "\n-- Internal State --\n");
    Msg(eDLL_T::CLIENT, "  Initialized     : %s\n", s_bInitialized ? "yes" : "no");
    Msg(eDLL_T::CLIENT, "  Module base     : 0x%p\n", reinterpret_cast<void*>(s_moduleBase));
    Msg(eDLL_T::CLIENT, "  World entity ptr: 0x%p\n", s_ppWorldEntity);
    Msg(eDLL_T::CLIENT, "  World entity    : 0x%p\n", s_ppWorldEntity ? *s_ppWorldEntity : nullptr);
    Msg(eDLL_T::CLIENT, "  Fallback func   : 0x%p\n", reinterpret_cast<void*>(v_RuiTrack_Fallback));

    // Additional diagnostics
    Msg(eDLL_T::CLIENT, "\n-- Diagnostics --\n");
    Msg(eDLL_T::CLIENT, "  g_pEngineClient     : 0x%p\n", g_pEngineClient);
    Msg(eDLL_T::CLIENT, "  g_pClientEntityList : 0x%p\n", g_pClientEntityList);
    Msg(eDLL_T::CLIENT, "  g_pClientState      : 0x%p\n", g_pClientState);
    Msg(eDLL_T::CLIENT, "  gpGlobals           : 0x%p\n", gpGlobals);
    if (gpGlobals)
    {
        Msg(eDLL_T::CLIENT, "  gpGlobals->curTime  : %.4f\n", gpGlobals->curTime);
        Msg(eDLL_T::CLIENT, "  gpGlobals->frameTime: %.6f\n", gpGlobals->frameTime);
    }

    CNetChan* pChan = RuiTracks_GetNetChannel();
    Msg(eDLL_T::CLIENT, "  NetChannel          : 0x%p\n", pChan);

    Msg(eDLL_T::CLIENT, "===============================================\n");
}

static ConCommand rui_track_debug(
    "rui_track_debug",
    CC_RuiTrackDebug_f,
    "Print current values of all custom RUI tracks",
    FCVAR_DEVELOPMENTONLY | FCVAR_CLIENTDLL
);

//-----------------------------------------------------------------------------
// Detour interface
//-----------------------------------------------------------------------------
void VRuiTracks::GetAdr(void) const
{
    LogFunAdr("RuiTrack_Fallback", v_RuiTrack_Fallback);
}

void VRuiTracks::GetFun(void) const { }
void VRuiTracks::GetVar(void) const { }
void VRuiTracks::GetCon(void) const { }

void VRuiTracks::Detour(const bool bAttach) const
{
    RuiTracks_Init();
    DetourSetup(&v_RuiTrack_Fallback, &RuiTrack_Fallback_Hook, bAttach);
}
