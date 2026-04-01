#include "core/stdafx.h"
#include "datacache/mdlcache.h"
#include "engine/staticpropmgr.h"
#include "engine/debugoverlay.h"
#include "tier1/cvar.h"

// ConVars to control debug text for static props
ConVar debug_staticprop_text("debug_staticprop_text", "0", FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT, "Enable debug text for static props");
ConVar debug_staticprop_text_duration("debug_staticprop_text_duration", "5.0", FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT, "Duration (seconds) for static prop debug text");

//-----------------------------------------------------------------------------
// Purpose: initialises static props from the static prop gamelump
//-----------------------------------------------------------------------------
void* CStaticProp::Init(CStaticProp* thisptr, int64_t a2, unsigned int idx, unsigned int a4, StaticPropLump_t* lump, int64_t a6, int64_t a7)
{
    MDLHandle_t handle = *reinterpret_cast<uint16_t*>(a7 + 0x140);
    studiohdr_t* pStudioHdr = g_pMDLCache->FindMDL(g_pMDLCache, handle, nullptr);

    if (lump->m_Skin >= pStudioHdr->numskinfamilies)
    {
        Error(eDLL_T::ENGINE, NO_ERROR,
            "Invalid skin index for static prop #%i with model '%s' (got %i, max %i)\n",
            idx, pStudioHdr->name, lump->m_Skin, pStudioHdr->numskinfamilies-1);

        lump->m_Skin = 0;
    }

    // Call original init first — some static props are initialised before
    // the debug overlay system is ready; adding the overlay afterwards
    // increases the chance the text persists and is rendered.
    void* ret = CStaticProp__Init(thisptr, a2, idx, a4, lump, a6, a7);

    // Show debug text next to static props so tools can inspect map-placed models.
    // Uses the existing debug overlay interface which respects `enable_debug_text_overlays`.
#ifndef DEDICATED
    if (pStudioHdr && g_pDebugOverlay && debug_staticprop_text.GetBool())
    {
        const Vector3D& origin = lump->m_Origin;
        const float duration = debug_staticprop_text_duration.GetFloat();
        CIVDebugOverlay::AddTextOverlayRGBu32(g_pDebugOverlay, origin, 0, duration, 255, 255, 0, 255, "staticprop %u: %s", idx, pStudioHdr->name);
    }
#endif // !DEDICATED

    return ret;
}

//-----------------------------------------------------------------------------
// NOTE: the following gather props functions have been hooked as we must
// enable the old gather props logic for fall back models to draw !!! The
// new solution won't call CMDLCache::GetHardwareData() on bad model handles.
//-----------------------------------------------------------------------------
void* GatherStaticPropsSecondPass_PreInit(GatherProps_t* gather)
{
    if (g_StudioMdlFallbackHandler.HasInvalidModelHandles())
        g_StudioMdlFallbackHandler.EnableLegacyGatherProps();

    return v_GatherStaticPropsSecondPass_PreInit(gather);
}
void* GatherStaticPropsSecondPass_PostInit(GatherProps_t* gather)
{
    if (g_StudioMdlFallbackHandler.HasInvalidModelHandles())
        g_StudioMdlFallbackHandler.EnableLegacyGatherProps();

    return v_GatherStaticPropsSecondPass_PostInit(gather);
}

void VStaticPropMgr::Detour(const bool bAttach) const
{
#ifndef DEDICATED
    DetourSetup(&CStaticProp__Init, &CStaticProp::Init, bAttach);
#endif // !DEDICATED

    DetourSetup(&v_GatherStaticPropsSecondPass_PreInit, &GatherStaticPropsSecondPass_PreInit, bAttach);
    DetourSetup(&v_GatherStaticPropsSecondPass_PostInit, &GatherStaticPropsSecondPass_PostInit, bAttach);
}
