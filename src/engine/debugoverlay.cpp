//============================================================================//
//
// Purpose: Debug interface functions
//
//============================================================================//

#include "core/stdafx.h"
#include "common/pseudodefs.h"
#include "tier0/memstd.h"
#include "tier0/basetypes.h"
#include "tier1/cvar.h"
#include "tier2/renderutils.h"
#include "mathlib/mathlib.h"
#ifndef DEDICATED
#include "engine/client/clientstate.h"
#endif // !DEDICATED
#include "engine/host_cmd.h"
#include "engine/cmodel.h"
#include "engine/debugoverlay.h"
#ifndef DEDICATED
#include "materialsystem/cmaterialsystem.h"
#endif // !DEDICATED
#ifndef CLIENT_DLL
#include "engine/server/server.h"
#include "game/server/entitylist.h"
#include "game/server/baseentity.h"
#endif // !CLIENT_DLL
#ifndef DEDICATED
#include "game/client/c_baseentity.h"
#include "game/client/cliententitylist.h"
#include "engine/cmodel_bsp_debug.h"
#endif // !DEDICATED
#if !defined(CLIENT_DLL) && !defined (DEDICATED)
#include "game/shared/ai_utility_shared.h"
#endif // !CLIENT_DLL && !DEDICATED

ConVar enable_debug_text_overlays("enable_debug_text_overlays", "0", FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT | FCVAR_GAMEDLL, "Enable rendering of debug text overlays");
static ConVar debug_overlay_nodecay("debug_overlay_nodecay", "0", FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT, "Keeps all debug overlays alive regardless of their lifetime. Use command 'clear_debug_overlays' to clear everything");

//------------------------------------------------------------------------------
// Purpose: returns whether the overlay can be added at this moment
//------------------------------------------------------------------------------
static bool DebugOverlay_CanApplyOverlay()
{
#ifndef DEDICATED
    if (!g_pClientState->IsPaused())
        return true;
#endif // !DEDICATED

#ifndef CLIENT_DLL
    if (g_pServer->CanApplyOverlays())
        return true;
#endif // !CLIENT_DLL

    return false;
}

//-----------------------------------------------------------------------------
// Purpose: determines and sets the end time for the overlay
//-----------------------------------------------------------------------------
template <class OverlayBaseClass>
static void DebugOverlay_SetEndTime(OverlayBaseClass* const base, const float duration, const bool nonTextOverlay)
{
    if (duration == 0.0f)
    {
        // note(kawe): the server runs in its own thread, and
        // at a different pace relative to the render thread.
        // DrawAllDebugOverlays() is the entry point, and the
        // only section where server debug overlays are being
        // added. This always runs in the server frame thread.
        // `g_nOverlayStage` has the correct pacing for server
        // overlays, this stage counter ensures the overlay
        // only draws for one frame, and does not render twice
        // in a frame causing the alpha to be multiplied.
        if (ThreadInServerFrameThread())
        {
            // note(kawe): this was originally 'n + 1' but this
            // makes debug text overlays to render for 2 frames
            // resulting in a trail effect when text is being
            // displayed on moving entities. Text is rendered
            // at a different point during the frame than the
            // non-text overlays causing this effect, so only
            // increment if we have a non-text overlay. For
            // non-text overlays we need the increment as it
            // ensures the server overlay runs for the entirety
            // of the client frame without rendering twice.
            base->m_nOverlayTick = (*g_nOverlayStage) + nonTextOverlay;	// stay alive for only one frame
        }
        else
        {
            // note(kawe): for client overlays, we must set the
            // start tick to the current render tick to ensure
            // it only renders once during its lifetime. Previously,
            // this was set to `g_nOverlayStage + 1`, however this
            // stage counter is meant to be used for server
            // overlays and will cause client overlays to render
            // twice sporadically when the frame times are low
            // enough. This causes a very apparent flickering
            // effect. In the `g_nOverlayStage` assignment above, I
            // added a comment regarding the extra increment, this
            // seems to be an effort to soften this effect on the
            // client, with the side effect of it rendering server
            // overlays twice consistently. This new method fixes
            // all these issues making the increment no longer needed.
            base->m_nCreationTick = *g_nRenderTickCount;
        }
    }
    else if (duration == (NDEBUG_PERSIST_TILL_NEXT_CLIENT))
    {
        base->m_nCreationTick = (*g_nRenderTickCount) + 1;
    }
    else if (duration == NDEBUG_PERSIST_TILL_NEXT_SERVER)
    {
        base->m_flEndTime = NDEBUG_PERSIST_TILL_NEXT_SERVER;
    }
    else
    {
#ifndef DEDICATED
        base->m_flEndTime = g_pClientState->GetClientTime() + duration;
#else
        base->m_flEndTime = g_pServer->GetTime();
#endif
    }
}

//-----------------------------------------------------------------------------
// Purpose: Hack to allow this code to run on a client that's not connected to a server
//  (i.e., demo playback, or multiplayer game )
// Input  : entNum - 
//          origin - 
//-----------------------------------------------------------------------------
static bool DebugOverlay_GetEntityOriginClientOrServer(const int entNum, Vector3D& origin)
{
#ifndef CLIENT_DLL
    if (g_pServer->IsActive())
    {
        const CEntInfo* const entInfo = g_serverEntityList->GetEntInfoPtrByIndex(entNum);
        const CBaseEntity* const serverEntity = (CBaseEntity*)entInfo->m_pEntity;

        if (!entInfo->m_pEntity)
            return false;

        CM_WorldSpaceCenter(serverEntity->CollisionProp(), &origin);
        return true;
    }
#endif // CLIENT_DLL

#ifndef DEDICATED
    IClientEntity* const clientEntity = g_clientEntityList->GetClientEntity(entNum);

    if (!clientEntity)
        return false;

    CM_WorldSpaceCenter(clientEntity->GetCollideable(), &origin);
#endif // DEDICATED

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: add new overlay sphere
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddSphereOverlayInternal(CIVDebugOverlay* const thisptr, const Vector3D& vOrigin, const float flRadius,
    const int nTheta, const int nPhi, const int r, const int g, const int b, const int a, const bool noDepthTest, const float flDuration)
{
    if (!DebugOverlay_CanApplyOverlay())
        return;

    AUTO_LOCK(*s_OverlayMutex);
    OverlaySphere_t* const newOverlay = new OverlaySphere_t;

    if (!newOverlay)
        return;

    newOverlay->vOrigin = vOrigin;
    newOverlay->flRadius = flRadius;
    newOverlay->nTheta = nTheta;
    newOverlay->nPhi = nPhi;
    newOverlay->r = r;
    newOverlay->g = g;
    newOverlay->b = b;
    newOverlay->a = a;
    newOverlay->noDepthTest = noDepthTest;

    newOverlay->SetEndTime(flDuration);

    newOverlay->m_pNextOverlay = *s_pOverlays;
    *s_pOverlays = newOverlay;
}

//-----------------------------------------------------------------------------
// Purpose: add new overlay swept box
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddSweptBoxInternal(CIVDebugOverlay* const thisptr, const Vector3D& start, const Vector3D& end, const Vector3D& mins,
    const Vector3D& max, const QAngle& angles, const int r, const int g, const int b, const int a, const bool noDepthTest, const float flDuration)
{
    if (!DebugOverlay_CanApplyOverlay())
        return;

    AUTO_LOCK(*s_OverlayMutex);
    OverlaySweptBox_t* const newOverlay = new OverlaySweptBox_t;

    if (!newOverlay)
        return;

    newOverlay->start = start;
    newOverlay->end = end;
    newOverlay->mins = mins;
    newOverlay->maxs = max;
    newOverlay->angles = angles;
    newOverlay->r = r;
    newOverlay->g = g;
    newOverlay->b = b;
    newOverlay->a = a;
    newOverlay->noDepthTest = noDepthTest;

    newOverlay->SetEndTime(flDuration);

    newOverlay->m_pNextOverlay = *s_pOverlays;
    *s_pOverlays = newOverlay;
}

//-----------------------------------------------------------------------------
// Purpose: add new overlay capsule
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddCapsuleOverlayInternal(CIVDebugOverlay* const thisptr, const Vector3D& vStart, const Vector3D& vEnd,
    const float flRadius, const int r, const int g, const int b, const int a, const bool noDepthTest, const float flDuration)
{
    if (!DebugOverlay_CanApplyOverlay())
        return;

    AUTO_LOCK(*s_OverlayMutex);
    OverlayCapsule_t* const newOverlay = new OverlayCapsule_t;

    if (!newOverlay)
        return;

    newOverlay->start = vStart;
    newOverlay->end = vEnd;
    newOverlay->radius = flRadius;
    newOverlay->r = r;
    newOverlay->g = g;
    newOverlay->b = b;
    newOverlay->a = a;
    newOverlay->noDepthTest = noDepthTest;

    newOverlay->SetEndTime(flDuration);

    newOverlay->m_pNextOverlay = *s_pOverlays;
    *s_pOverlays = newOverlay;
}

//------------------------------------------------------------------------------
// Purpose: checks if overlay should be decayed
// Output : true to decay, false otherwise
//------------------------------------------------------------------------------
bool OverlayBase_t::IsDead() const
{
    if (debug_overlay_nodecay.GetBool())
    {
        // Keep rendering the overlay if no-decay is set.
        return false;
    }

    if (m_nCreationTick != -1)
        return m_nCreationTick < *g_nRenderTickCount;

    if (m_nOverlayTick != -1)
        return m_nOverlayTick < *g_nOverlayTickCount;

    if (!DebugOverlay_CanApplyOverlay())
    {
        // Keep rendering the overlay if the simulation is paused.
        return false;
    }

    if (m_flEndTime == NDEBUG_PERSIST_TILL_NEXT_SERVER)
    {
        return false;
    }

#ifndef DEDICATED
    return m_flEndTime < g_pClientState->GetClientTime();
#else
    return m_flEndTime < g_pServer->GetTime();
#endif
}

//------------------------------------------------------------------------------
// Purpose: sets the shape overlay end time
// Input  : duration
//------------------------------------------------------------------------------
void OverlayBase_t::SetEndTime(const float duration)
{
    (*g_nNewOtherOverlays)++;
    DebugOverlay_SetEndTime(this, duration, true);
}

//------------------------------------------------------------------------------
// Purpose: sets the text overlay end time
// Input  : duration
//------------------------------------------------------------------------------
void OverlayText_t::SetEndTime(const float duration)
{
    (*g_nNewTextOverlays)++;
    DebugOverlay_SetEndTime(this, duration, false);
}

//------------------------------------------------------------------------------
// Purpose: detour proxy for setting the overlay's end time
//------------------------------------------------------------------------------
static void DebugOverlay_SetEndTime(OverlayBase_t* const pOverlay, const float flDuration)
{
    pOverlay->SetEndTime(flDuration);
}

//------------------------------------------------------------------------------
// Purpose: destroys the overlay
// Input  : *pOverlay - 
//------------------------------------------------------------------------------
static void DebugOverlay_DestroyOverlay(OverlayBase_t* const pOverlay)
{
    AUTO_LOCK(*s_OverlayMutex);
    switch (pOverlay->m_Type)
    {
    case OverlayType_t::OVERLAY_BOX:
    case OverlayType_t::OVERLAY_SPHERE:
    case OverlayType_t::OVERLAY_LINE:
    case OverlayType_t::OVERLAY_CUSTOM_MESH:
    case OverlayType_t::OVERLAY_TRIANGLE:
    case OverlayType_t::OVERLAY_SWEPT_BOX:
    case OverlayType_t::OVERLAY_CAPSULE:
        pOverlay->m_Type = OverlayType_t::OVERLAY_DESTROYED;
        delete pOverlay;

        break;
        // Splines aren't allocated, they are stored in s_splineOverlays
        // which is a static array of 300 * OverlayLine_t. Just mark it
        // destroyed here so the spline item can be reused.
    case OverlayType_t::OVERLAY_SPLINE:
        pOverlay->m_Type = OverlayType_t::OVERLAY_DESTROYED;
        break;
    default:
        Assert(0); // Code bug; invalid overlay type.
        break;
    }
}

//------------------------------------------------------------------------------
// Purpose: draws a generic overlay
// Input  : *pOverlay - 
//------------------------------------------------------------------------------
static void DebugOverlay_DrawOverlay(const OverlayBase_t* const pOverlay)
{
    AUTO_LOCK(*s_OverlayMutex);

    switch (pOverlay->m_Type)
    {
    case OverlayType_t::OVERLAY_BOX:
    {
        const OverlayBox_t* const pBox = static_cast<const OverlayBox_t*>(pOverlay);

        if (pBox->a > 0)
        {
            RenderBox(pBox->transforms, pBox->mins, pBox->maxs, Color(pBox->r, pBox->g, pBox->b, pBox->a), !pBox->noDepthTest);
        }
        if (pBox->a < 255)
        {
            RenderWireframeBox(pBox->transforms, pBox->mins, pBox->maxs, Color(pBox->r, pBox->g, pBox->b, 255), !pBox->noDepthTest);
        }

        break;
    }
    case OverlayType_t::OVERLAY_SPHERE:
    {
        const OverlaySphere_t* const pSphere = static_cast<const OverlaySphere_t*>(pOverlay);

        if (pSphere->a > 0)
        {
            RenderSphere(pSphere->vOrigin, pSphere->flRadius, pSphere->nTheta, pSphere->nPhi,
                Color(pSphere->r, pSphere->g, pSphere->b, pSphere->a), !pSphere->noDepthTest);
        }
        if (pSphere->a < 255)
        {
            RenderWireframeSphere(pSphere->vOrigin, pSphere->flRadius, pSphere->nTheta, pSphere->nPhi,
                Color(pSphere->r, pSphere->g, pSphere->b, 255), !pSphere->noDepthTest);
        }

        break;
    }
    case OverlayType_t::OVERLAY_LINE:
    {
        const OverlayLine_t* const pLine = static_cast<const OverlayLine_t*>(pOverlay);
        RenderLine(pLine->origin, pLine->dest, Color(pLine->r, pLine->g, pLine->b, pLine->a), !pLine->noDepthTest);

        break;
    }
    case OverlayType_t::OVERLAY_CUSTOM_MESH:
    {
        // TODO: 128 * matrix3x4_t, figure out how to render this...
        // Nothing in the game is currently calling this overlay, so
        // implementing this isn't necessary.
        break;
    }
    case OverlayType_t::OVERLAY_SPLINE:
    {
        // This is used for the Smart Pistol laser.
        const OverlayLine_t* const pSpline = reinterpret_cast<const OverlayLine_t*>(pOverlay);
        RenderLine(pSpline->origin, pSpline->dest, Color(pSpline->r, pSpline->g, pSpline->b, pSpline->a), !pSpline->noDepthTest);

        break;
    }
    case OverlayType_t::OVERLAY_TRIANGLE:
    {
        const OverlayTriangle_t* const pTriangle = reinterpret_cast<const OverlayTriangle_t*>(pOverlay);
        RenderTriangle(pTriangle->p1, pTriangle->p2, pTriangle->p3, Color(pTriangle->r, pTriangle->g, pTriangle->b, pTriangle->a), !pTriangle->noDepthTest);

        break;
    }
    case OverlayType_t::OVERLAY_SWEPT_BOX:
    {
        const OverlaySweptBox_t* const pSweptBox = reinterpret_cast<const OverlaySweptBox_t*>(pOverlay);
        RenderWireframeSweptBox(pSweptBox->start, pSweptBox->end, pSweptBox->angles, pSweptBox->mins, pSweptBox->maxs,
            Color(pSweptBox->r, pSweptBox->g, pSweptBox->b, pSweptBox->a), !pSweptBox->noDepthTest);
        break;
    }
    case OverlayType_t::OVERLAY_CAPSULE:
    {
        const OverlayCapsule_t* const pCapsule = static_cast<const OverlayCapsule_t*>(pOverlay);
        RenderCapsule(pCapsule->start, pCapsule->end, pCapsule->radius, Color(pCapsule->r, pCapsule->g, pCapsule->b, pCapsule->a), !pCapsule->noDepthTest);

        break;
    }
    }
}

//------------------------------------------------------------------------------
// Purpose : overlay drawing and decaying entry point
// Input   : bDraw - only runs the decaying logic if false
//------------------------------------------------------------------------------
static void DebugOverlay_DrawAllOverlays(const bool bDraw)
{
    AUTO_LOCK(*s_OverlayMutex);

    const bool bOverlayEnabled = (bDraw && enable_debug_overlays->GetBool());
    OverlayBase_t* pCurrOverlay = *s_pOverlays;
    OverlayBase_t* pPrevOverlay = nullptr;
    OverlayBase_t* pNextOverlay = nullptr;

    while (pCurrOverlay)
    {
        // Is it time to kill this overlay?
        if (pCurrOverlay->IsDead())
        {
            if (pPrevOverlay)
            {
                // If I had a last overlay reset it's next pointer
                pPrevOverlay->m_pNextOverlay = pCurrOverlay->m_pNextOverlay;
            }
            else
            {
                // If the first line, reset the s_pOverlays pointer
                *s_pOverlays = pCurrOverlay->m_pNextOverlay;
            }

            pNextOverlay = pCurrOverlay->m_pNextOverlay;
            DebugOverlay_DestroyOverlay(pCurrOverlay);
            pCurrOverlay = pNextOverlay;
        }
        else
        {
            if (bOverlayEnabled)
            {
                bool bShouldDraw = false;

                if (pCurrOverlay->m_nCreationTick == -1)
                {
                    if (pCurrOverlay->m_nOverlayTick == *g_nOverlayTickCount ||
                        pCurrOverlay->m_nOverlayTick == -1)
                    {
                        bShouldDraw = true;
                    }
                }
                else
                {
                    bShouldDraw = pCurrOverlay->m_nCreationTick == *g_nRenderTickCount;
                }
                if (bShouldDraw)
                {
                    DebugOverlay_DrawOverlay(pCurrOverlay);
                }
            }

            pPrevOverlay = pCurrOverlay;
            pCurrOverlay = pCurrOverlay->m_pNextOverlay;
        }
    }

    g_pDebugOverlay->ClearDeadTextOverlays();

#if !defined(CLIENT_DLL) && !defined (DEDICATED)
    if (bOverlayEnabled)
    {
        g_AIUtility.RunRenderFrame();
    }
#endif // !CLIENT_DLL && !DEDICATED

#ifndef DEDICATED
    // BSP collision debug rendering
    CBSPCollisionDebug::Render();
#endif // !DEDICATED
}

//------------------------------------------------------------------------------
// Purpose : clear dead overlays
//------------------------------------------------------------------------------
static void DebugOverlay_ClearDeadOverlays()
{
    AUTO_LOCK(*s_OverlayMutex);

    OverlayBase_t* pCurrOverlay = *s_pOverlays;
    OverlayBase_t* pPrevOverlay = nullptr;
    OverlayBase_t* pNextOverlay = nullptr;

    while (pCurrOverlay)
    {
        // Is it time to kill this overlay?
        if (pCurrOverlay->IsDead())
        {
            if (pPrevOverlay)
            {
                // If I had a last overlay reset it's next pointer
                pPrevOverlay->m_pNextOverlay = pCurrOverlay->m_pNextOverlay;
            }
            else
            {
                // If the first line, reset the s_pOverlays pointer
                *s_pOverlays = pCurrOverlay->m_pNextOverlay;
            }

            pNextOverlay = pCurrOverlay->m_pNextOverlay;
            DebugOverlay_DestroyOverlay(pCurrOverlay);
            pCurrOverlay = pNextOverlay;
        }
        else
        {
            pPrevOverlay = pCurrOverlay;
            pCurrOverlay = pCurrOverlay->m_pNextOverlay;
        }
    }
}

//-----------------------------------------------------------------------------
// Purpose: clears all overlays
//-----------------------------------------------------------------------------
static void DebugOverlay_ClearAllOverlays()
{
    AUTO_LOCK(*s_OverlayMutex);

    while (*s_pOverlays)
    {
        OverlayBase_t* pOldOverlay = *s_pOverlays;
        *s_pOverlays = (*s_pOverlays)->m_pNextOverlay;
        DebugOverlay_DestroyOverlay(pOldOverlay);
    }

    while (*s_pOverlayText)
    {
        OverlayText_t* cur_ol = *s_pOverlayText;
        *s_pOverlayText = (*s_pOverlayText)->nextOverlayText;
        delete cur_ol;
    }

    *s_bDrawGrid = false;
}

//------------------------------------------------------------------------------
// Purpose : clear all dead overlays; this is a separate version of the decaying
//           logic found in DebugOverlay_DrawAllOverlays(). The dedicated server
//           needs to call this function as DebugOverlay_DrawAllOverlays() won't
//           be called as this is initiated from CViewRender, which is not on.
//------------------------------------------------------------------------------
void DebugOverlay_HandleDecayed()
{
    // These must always be called, even when the debug overlay is disabled
    // because the calls to the debug interface still take place. Its up to
    // the engine and SDK to deal with these calls. Not calling these will
    // cause overlays to stack up forever.
    DebugOverlay_ClearDeadOverlays();
    g_pDebugOverlay->ClearDeadTextOverlays();
}

//-----------------------------------------------------------------------------
// Purpose: internal wrapper for adding new world positioned overlay text
//-----------------------------------------------------------------------------
static void DebugOverlay_AddTextOverlay(const Vector3D& pos, const int lineOffset, const float duration,
    const int r, const int g, const int b, const int a, const char* const text, const ssize_t textLen)
{
    OverlayText_t* const newOverlay = new OverlayText_t;

    if (!newOverlay)
        return;

    VectorCopy(pos, newOverlay->origin);

    newOverlay->textLen = textLen;
    newOverlay->textBuf = new char[textLen + 1];

    if (!newOverlay->textBuf)
    {
        delete newOverlay;
        return;
    }

    Q_strncpy(newOverlay->textBuf, text, textLen + 1);

    newOverlay->bUseOrigin = true;
    newOverlay->lineOffset = lineOffset;

    newOverlay->SetEndTime(duration);

    newOverlay->r = r;
    newOverlay->g = g;
    newOverlay->b = b;
    newOverlay->a = a;

    newOverlay->nextOverlayText = *s_pOverlayText;
    *s_pOverlayText = newOverlay;
}

//-----------------------------------------------------------------------------
// Purpose: add new entity positioned overlay text
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddEntityTextOverlay(CIVDebugOverlay* const thisptr, const int entIndex, const int lineOffset, const float duration, 
                                            const int r, const int g, const int b, const int a, const char* const format, ...)
{
    if (!enable_debug_text_overlays.GetBool() || !DebugOverlay_CanApplyOverlay())
        return;

    Vector3D pos;

    if (!DebugOverlay_GetEntityOriginClientOrServer(entIndex, pos))
        return;

    AUTO_LOCK(*s_OverlayMutex);

    va_start(thisptr->m_argptr, format);
    const int textLen = Q_vsnprintf(thisptr->m_text, sizeof(thisptr->m_text), format, thisptr->m_argptr);
    va_end(thisptr->m_argptr);

    if (textLen > 0)
        DebugOverlay_AddTextOverlay(pos, lineOffset, duration, r, g, b, a, thisptr->m_text, textLen);
}

//-----------------------------------------------------------------------------
// Purpose: add new world positioned overlay text
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddTextOverlay(CIVDebugOverlay* const thisptr, const Vector3D& origin, const float duration, const char* const format, ...)
{
    if (!enable_debug_text_overlays.GetBool() || !DebugOverlay_CanApplyOverlay())
        return;

    AUTO_LOCK(*s_OverlayMutex);

    va_start(thisptr->m_argptr, format);
    const int textLen = Q_vsnprintf(thisptr->m_text, sizeof(thisptr->m_text), format, thisptr->m_argptr);
    va_end(thisptr->m_argptr);

    if (textLen > 0)
        DebugOverlay_AddTextOverlay(origin, 0, duration, 255, 255, 255, 255, thisptr->m_text, textLen);
}

//-----------------------------------------------------------------------------
// Purpose: add new world positioned overlay text at line offset
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddTextOverlayAtOffset(CIVDebugOverlay* const thisptr, const Vector3D& origin, const int lineOffset, const float duration, const char* const format, ...)
{
    if (!enable_debug_text_overlays.GetBool() || !DebugOverlay_CanApplyOverlay())
        return;

    AUTO_LOCK(*s_OverlayMutex);

    va_start(thisptr->m_argptr, format);
    const int textLen = Q_vsnprintf(thisptr->m_text, sizeof(thisptr->m_text), format, thisptr->m_argptr);
    va_end(thisptr->m_argptr);

    if (textLen > 0)
        DebugOverlay_AddTextOverlay(origin, lineOffset, duration, 255, 255, 255, 255, thisptr->m_text, textLen);
}

//-----------------------------------------------------------------------------
// Purpose: add new world positioned overlay text using 32 bit color
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddTextOverlayRGBu32(CIVDebugOverlay* const thisptr, const Vector3D& origin, const int lineOffset, const float duration, 
    const int r, const int g, const int b, const int a, PRINTF_FORMAT_STRING const char* const format, ...) FMTFUNCTION(9, 10)
{
    if (!enable_debug_text_overlays.GetBool())
        return;

    AUTO_LOCK(*s_OverlayMutex);

    va_start(thisptr->m_argptr, format);
    const int textLen = Q_vsnprintf(thisptr->m_text, sizeof(thisptr->m_text), format, thisptr->m_argptr);
    va_end(thisptr->m_argptr);

    if (textLen > 0)
        DebugOverlay_AddTextOverlay(origin, lineOffset, duration, r, g, b, a, thisptr->m_text, textLen);
}

//-----------------------------------------------------------------------------
// Purpose: add new world positioned overlay text using float color
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddTextOverlayRGBf32(CIVDebugOverlay* const thisptr, const Vector3D& origin, const int lineOffset, const float duration,
    const float r, const float g, const float b, const float a, PRINTF_FORMAT_STRING const char* const format, ...) FMTFUNCTION(8, 9)
{
    if (!enable_debug_text_overlays.GetBool() || !DebugOverlay_CanApplyOverlay())
        return;

    AUTO_LOCK(*s_OverlayMutex);

    va_start(thisptr->m_argptr, format);
    const int textLen = Q_vsnprintf(thisptr->m_text, sizeof(thisptr->m_text), format, thisptr->m_argptr);
    va_end(thisptr->m_argptr);

    if (textLen > 0)
    {
        const int cr = (int)Clamp(r * 255.f, 0.f, 255.f);
        const int cg = (int)Clamp(g * 255.f, 0.f, 255.f);
        const int cb = (int)Clamp(b * 255.f, 0.f, 255.f);
        const int ca = (int)Clamp(a * 255.f, 0.f, 255.f);

        DebugOverlay_AddTextOverlay(origin, lineOffset, duration, cr, cg, cb, ca, thisptr->m_text, textLen);
    }
}

//-----------------------------------------------------------------------------
// Purpose: internal wrapper for adding new screen positioned overlay text
//-----------------------------------------------------------------------------
static void DebugOverlay_AddScreenTextOverlay(const float flXpos, const float flYpos, const int lineOffset,
    const float duration, const int r, const int g, const int b, const int a, const char* const text, const ssize_t textLen)
{
    OverlayText_t* const newOverlay = new OverlayText_t;

    if (!newOverlay)
        return;

    newOverlay->screenPos.Init(flXpos, flYpos);

    newOverlay->textLen = textLen;
    newOverlay->textBuf = new char[textLen + 1];

    if (!newOverlay->textBuf)
    {
        delete newOverlay;
        return;
    }

    Q_strncpy(newOverlay->textBuf, text, textLen + 1);

    newOverlay->bUseOrigin = false;
    newOverlay->lineOffset = lineOffset;

    newOverlay->SetEndTime(duration);

    newOverlay->r = r;
    newOverlay->g = g;
    newOverlay->b = b;
    newOverlay->a = a;

    newOverlay->nextOverlayText = *s_pOverlayText;
    *s_pOverlayText = newOverlay;
}

//-----------------------------------------------------------------------------
// Purpose: add new screen positioned overlay text at offset
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddScreenTextOverlayAtOffsetInternal(CIVDebugOverlay* const thisptr, const float flXPos, const float flYPos,
    const int lineOffset, const float flDuration, const int r, const int g, const int b, const int a, const char* const text)
{
    if (!enable_debug_text_overlays.GetBool() || !DebugOverlay_CanApplyOverlay())
        return;

    const ssize_t textLen = (ssize_t)strlen(text);

    if (textLen < 1)
        return; // Empty.

    AUTO_LOCK(*s_OverlayMutex);
    DebugOverlay_AddScreenTextOverlay(flXPos, flYPos, lineOffset, flDuration, r, g, b, a, text, textLen);
}

//-----------------------------------------------------------------------------
// Purpose: add new screen positioned overlay text
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddScreenTextOverlayInternal(CIVDebugOverlay* const thisptr, const float flXPos, const float flYPos, const float flDuration, const int r, const int g, const int b, const int a, const char* const text)
{
    if (!enable_debug_text_overlays.GetBool() || !DebugOverlay_CanApplyOverlay())
        return;

    const ssize_t textLen = (ssize_t)strlen(text);

    if (textLen < 1)
        return; // Empty.

    AUTO_LOCK(*s_OverlayMutex);
    DebugOverlay_AddScreenTextOverlay(flXPos, flYPos, 0, flDuration, r, g, b, a, text, textLen);
}

//-----------------------------------------------------------------------------
// These are the same as above, except we have to shift the 'this' pointer back
// with sizeof(void*) bytes because we call CIVDebugOverlay methods which uses
// its member variables, IVPhysicsDebugOverlay methods will have the thisptr
// shifted with 8 bytes forward due to compiler optimizations. Only functions
// using member variables or IVDebugOverlay methods have been duplicated here.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: add new entity positioned overlay text
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddPhysicsEntityTextOverlay(CIVDebugOverlay* const thisptr, const int entIndex, const int lineOffset, const float duration, const int r, const int g, const int b, const int a, const char* const format, ...)
{
    if (!enable_debug_text_overlays.GetBool() || !DebugOverlay_CanApplyOverlay())
        return;

    Vector3D pos;

    if (!DebugOverlay_GetEntityOriginClientOrServer(entIndex, pos))
        return;

    AUTO_LOCK(*s_OverlayMutex);
    CIVDebugOverlay* const thisprAdj = (CIVDebugOverlay*)((intptr_t)(thisptr)-sizeof(void*));

    va_start(thisprAdj->m_argptr, format);
    const int textLen = Q_vsnprintf(thisprAdj->m_text, sizeof(thisprAdj->m_text), format, thisprAdj->m_argptr);
    va_end(thisprAdj->m_argptr);

    if (textLen > 0)
        DebugOverlay_AddTextOverlay(pos, lineOffset, duration, r, g, b, a, thisprAdj->m_text, textLen);
}

//-----------------------------------------------------------------------------
// Purpose: add new world positioned overlay text
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddPhysicsTextOverlay(CIVDebugOverlay* const thisptr, const Vector3D& origin, const float duration, const char* const format, ...)
{
    if (!enable_debug_text_overlays.GetBool() || !DebugOverlay_CanApplyOverlay())
        return;

    AUTO_LOCK(*s_OverlayMutex);
    CIVDebugOverlay* const thisprAdj = (CIVDebugOverlay*)((intptr_t)(thisptr)-sizeof(void*));

    va_start(thisprAdj->m_argptr, format);
    const int textLen = Q_vsnprintf(thisprAdj->m_text, sizeof(thisprAdj->m_text), format, thisprAdj->m_argptr);
    va_end(thisprAdj->m_argptr);

    if (textLen > 0)
        DebugOverlay_AddTextOverlay(origin, 0, duration, 255, 255, 255, 255, thisprAdj->m_text, textLen);
}

//-----------------------------------------------------------------------------
// Purpose: add new world positioned overlay text at line offset
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddPhysicsTextOverlayAtOffset(CIVDebugOverlay* const thisptr, const Vector3D& origin, const int lineOffset, const float duration, const char* const format, ...)
{
    if (!enable_debug_text_overlays.GetBool() || !DebugOverlay_CanApplyOverlay())
        return;

    AUTO_LOCK(*s_OverlayMutex);
    CIVDebugOverlay* const thisprAdj = (CIVDebugOverlay*)((intptr_t)(thisptr)-sizeof(void*));

    va_start(thisprAdj->m_argptr, format);
    const int textLen = Q_vsnprintf(thisprAdj->m_text, sizeof(thisprAdj->m_text), format, thisprAdj->m_argptr);
    va_end(thisprAdj->m_argptr);

    if (textLen > 0)
        DebugOverlay_AddTextOverlay(origin, lineOffset, duration, 255, 255, 255, 255, thisprAdj->m_text, textLen);
}

//-----------------------------------------------------------------------------
// Purpose: add new world positioned overlay text using float color
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddPhysicsTextOverlayRGBf32(CIVDebugOverlay* const thisptr, const Vector3D& origin, const int lineOffset, const float duration,
    const float r, const float g, const float b, const float a, PRINTF_FORMAT_STRING const char* const format, ...) FMTFUNCTION(8, 9)
{
    if (!enable_debug_text_overlays.GetBool() || !DebugOverlay_CanApplyOverlay())
        return;

    AUTO_LOCK(*s_OverlayMutex);
    CIVDebugOverlay* const thisprAdj = (CIVDebugOverlay*)((intptr_t)(thisptr)-sizeof(void*));

    va_start(thisprAdj->m_argptr, format);
    const int textLen = Q_vsnprintf(thisprAdj->m_text, sizeof(thisprAdj->m_text), format, thisprAdj->m_argptr);
    va_end(thisprAdj->m_argptr);

    if (textLen > 0)
    {
        const int cr = (int)Clamp(r * 255.f, 0.f, 255.f);
        const int cg = (int)Clamp(g * 255.f, 0.f, 255.f);
        const int cb = (int)Clamp(b * 255.f, 0.f, 255.f);
        const int ca = (int)Clamp(a * 255.f, 0.f, 255.f);

        DebugOverlay_AddTextOverlay(origin, lineOffset, duration, cr, cg, cb, ca, thisprAdj->m_text, textLen);
    }
}

///////////////////////////////////////////////////////////////////////////////
void VDebugOverlay::Detour(const bool bAttach) const
{
    DetourSetup(&v_DebugOverlay_DrawAllOverlays, &DebugOverlay_DrawAllOverlays, bAttach);
    DetourSetup(&v_DebugOverlay_ClearAllOverlays, &DebugOverlay_ClearAllOverlays, bAttach);
    DetourSetup(&v_DebugOverlay_SetEndTime, &DebugOverlay_SetEndTime, bAttach);

    if (bAttach)
    {
        void* null;

        // Replace the nulled functions in the IVPhysicsDebugOverlay implementation with ours.
        CMemory::HookVirtualMethod((uintptr_t)g_pIVPhysicsDebugOverlay_VFTable, CIVDebugOverlay::AddPhysicsEntityTextOverlay, 0, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVPhysicsDebugOverlay_VFTable, CIVDebugOverlay::AddPhysicsTextOverlay, 4, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVPhysicsDebugOverlay_VFTable, CIVDebugOverlay::AddPhysicsTextOverlayAtOffset, 5, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVPhysicsDebugOverlay_VFTable, CIVDebugOverlay::AddScreenTextOverlayInternal, 6, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVPhysicsDebugOverlay_VFTable, CIVDebugOverlay::AddSweptBoxInternal, 7, &null); // NEW: now supports setting depth testing.
        CMemory::HookVirtualMethod((uintptr_t)g_pIVPhysicsDebugOverlay_VFTable, CIVDebugOverlay::AddPhysicsTextOverlayRGBf32, 8, &null);

        // Replace the nulled functions in the IVDebugOverlay implementation with ours.
        CMemory::HookVirtualMethod((uintptr_t)g_pIVDebugOverlay_VFTable, CIVDebugOverlay::AddEntityTextOverlay, 0, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVDebugOverlay_VFTable, CIVDebugOverlay::AddSphereOverlayInternal, 3, &null); // NEW: now supports setting depth testing.
        CMemory::HookVirtualMethod((uintptr_t)g_pIVDebugOverlay_VFTable, CIVDebugOverlay::AddTextOverlayAtOffset, 8, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVDebugOverlay_VFTable, CIVDebugOverlay::AddTextOverlay, 9, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVDebugOverlay_VFTable, CIVDebugOverlay::AddScreenTextOverlayAtOffsetInternal , 10, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVDebugOverlay_VFTable, CIVDebugOverlay::AddScreenTextOverlayInternal, 11, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVDebugOverlay_VFTable, CIVDebugOverlay::AddSweptBoxInternal, 12, &null); // NEW: now supports setting depth testing.
        CMemory::HookVirtualMethod((uintptr_t)g_pIVDebugOverlay_VFTable, CIVDebugOverlay::AddTextOverlayRGBu32, 24, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVDebugOverlay_VFTable, CIVDebugOverlay::AddTextOverlayRGBf32, 25, &null);

        // The overlay adder at index 27 is unknown and never used, its renderer also doesn't
        // exist. Replaced with capsule renderer allowing us to add these through the interface.
        CMemory::HookVirtualMethod((uintptr_t)g_pIVDebugOverlay_VFTable, CIVDebugOverlay::AddCapsuleOverlayInternal, 27, &null);
    }
}
