//=============================================================================//
//
// Purpose: Extended model precache system to support more than 4096 models
// 
// Based on cafe's SDK changes to fix +s13 map crash.
// This system extends the model precache limit from 4096 to 16384 by:
// 1. Patching the modelprecache string table limit from 4096 to 16384
// 2. Providing extended storage for model indices >= 4096 
// 3. Hooking GetModel/PrecacheModel functions to redirect lookups to extended storage
//
//=============================================================================//

#include "core/stdafx.h"
#include "tier1/cvar.h"
#include "engine/gl_model_private.h"
#include "engine/sv_model_precache_ext.h"
#include "engine/modelloader.h"
#ifndef CLIENT_DLL
#include "engine/server/server.h"
#endif // !CLIENT_DLL
#ifndef DEDICATED
#include "engine/client/clientstate.h"
#endif // !DEDICATED

//-----------------------------------------------------------------------------
// ConVars
//-----------------------------------------------------------------------------
static ConVar sv_model_precache_ext_debug("sv_model_precache_ext_debug", "0", FCVAR_DEVELOPMENTONLY, 
    "Debug output for extended model precache system");

//-----------------------------------------------------------------------------
// Memory patch locations
//-----------------------------------------------------------------------------
static void* s_pModelPrecacheLimitPatch = nullptr;  // Location of "mov r9d, 1000h" for modelprecache limit

//-----------------------------------------------------------------------------
// Extended storage for models with indices >= 4096
//-----------------------------------------------------------------------------
#ifndef CLIENT_DLL
CPrecacheItem g_ExtendedServerModelPrecache[MODEL_PRECACHE_EXTENDED_COUNT];
#endif // !CLIENT_DLL

#ifndef DEDICATED
CPrecacheItem g_ExtendedClientModelPrecache[MODEL_PRECACHE_EXTENDED_COUNT];
#endif // !DEDICATED

//-----------------------------------------------------------------------------
// Purpose: Clear extended server precache storage
//-----------------------------------------------------------------------------
void SV_ModelPrecacheExt_ClearServer()
{
#ifndef CLIENT_DLL
    for (int i = 0; i < MODEL_PRECACHE_EXTENDED_COUNT; i++)
    {
        g_ExtendedServerModelPrecache[i].Clear();
    }

    if (sv_model_precache_ext_debug.GetBool())
    {
        DevMsg(eDLL_T::ENGINE, "Extended server model precache storage cleared\n");
    }
#endif // !CLIENT_DLL
}

//-----------------------------------------------------------------------------
// Purpose: Clear extended client precache storage
//-----------------------------------------------------------------------------
void SV_ModelPrecacheExt_ClearClient()
{
#ifndef DEDICATED
    for (int i = 0; i < MODEL_PRECACHE_EXTENDED_COUNT; i++)
    {
        g_ExtendedClientModelPrecache[i].Clear();
    }

    if (sv_model_precache_ext_debug.GetBool())
    {
        DevMsg(eDLL_T::ENGINE, "Extended client model precache storage cleared\n");
    }
#endif // !DEDICATED
}

//-----------------------------------------------------------------------------
// Purpose: Initialize the extended precache system
//-----------------------------------------------------------------------------
void SV_ModelPrecacheExt_Init()
{
    SV_ModelPrecacheExt_ClearServer();
    SV_ModelPrecacheExt_ClearClient();
    
    DevMsg(eDLL_T::ENGINE, "Extended model precache system initialized (limit: %d -> %d)\n",
        MODEL_PRECACHE_BASE_LIMIT, MODEL_PRECACHE_TOTAL_LIMIT);
}

//-----------------------------------------------------------------------------
// Purpose: Shutdown the extended precache system
//-----------------------------------------------------------------------------
void SV_ModelPrecacheExt_Shutdown()
{
    SV_ModelPrecacheExt_ClearServer();
    SV_ModelPrecacheExt_ClearClient();
}

#ifndef CLIENT_DLL
//-----------------------------------------------------------------------------
// Purpose: Hook for CServer::PrecacheModel - logs extended model precaching
// Input  : *thisptr - CServer instance
//          *szModelName - model path to precache
// Output : model index (can be >= 4096 for extended models)
//-----------------------------------------------------------------------------
int CServer_PrecacheModel_Hook(CServer* thisptr, const char* szModelName)
{
    // Call original function first
    int modelIndex = CServer__PrecacheModel(thisptr, szModelName);
    
    // Log extended model indices for debugging
    if (modelIndex >= MODEL_PRECACHE_BASE_LIMIT && modelIndex < MODEL_PRECACHE_TOTAL_LIMIT)
    {
        if (sv_model_precache_ext_debug.GetBool())
        {
            DevMsg(eDLL_T::ENGINE, "CServer::PrecacheModel: Extended model %d precached -> %s\n",
                modelIndex, szModelName);
        }
    }
    
    return modelIndex;
}
#endif // !CLIENT_DLL

//-----------------------------------------------------------------------------
// Purpose: Find and set up function pointers via pattern scanning
//-----------------------------------------------------------------------------
void VSV_ModelPrecacheExt::GetFun(void) const
{
    // Pattern for CServer::PrecacheModel
    // Signature: 48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 48 8B D9 48 C7 44 24 20 00 00 00 00 48 8B 89
#ifndef CLIENT_DLL
    Module_FindPattern(g_GameDll, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 48 8B D9 48 C7 44 24 ?? 00 00 00 00 48 8B 89")
        .GetPtr(CServer__PrecacheModel);
#endif // !CLIENT_DLL

    // Pattern for modelprecache string table limit
    // This pattern finds the "mov r9d, 1000h" instruction that sets the modelprecache limit to 4096
    // Pattern: lea r8, "modelprecache" ; mov [rsp+30h], 1 ; mov r9d, 1000h
    // We find this and patch it to 16384 (0x4000)
    CMemory modelPrecacheLimitMem = Module_FindPattern(g_GameDll, 
        "4C 8D 05 ?? ?? ?? ?? C7 44 24 30 01 00 00 00 41 B9 00 10 00 00");
    
    if (modelPrecacheLimitMem)
    {
        // The "41 B9 00 10 00 00" (mov r9d, 1000h) is at offset +17 from pattern start
        s_pModelPrecacheLimitPatch = modelPrecacheLimitMem.Offset(0x11).RCast<void*>();
        
        if (sv_model_precache_ext_debug.GetBool())
        {
            DevMsg(eDLL_T::ENGINE, "Found modelprecache limit at %p\n", s_pModelPrecacheLimitPatch);
        }
    }
    else
    {
        Warning(eDLL_T::ENGINE, "Failed to find modelprecache limit pattern - extended model support disabled\n");
    }
}

//-----------------------------------------------------------------------------
// Purpose: Set up detours and memory patches for the extended model system
//-----------------------------------------------------------------------------
void VSV_ModelPrecacheExt::Detour(const bool bAttach) const
{
    // Patch the modelprecache string table limit from 4096 to 16384
    if (s_pModelPrecacheLimitPatch)
    {
        if (bAttach)
        {
            // s_pModelPrecacheLimitPatch points to the immediate value in the instruction
            // Original immediate: 00 10 00 00 (little-endian 0x1000 = 4096)
            // Patched immediate:  00 40 00 00 (little-endian 0x4000 = 16384)
            uint8_t* pLimit = reinterpret_cast<uint8_t*>(s_pModelPrecacheLimitPatch);
            
            DWORD oldProtect;
            VirtualProtect(pLimit, 4, PAGE_EXECUTE_READWRITE, &oldProtect);
            
            // pLimit[0] = 0x00, pLimit[1] = 0x10 <- change to 0x40, pLimit[2] = 0x00, pLimit[3] = 0x00
            pLimit[1] = 0x40;  // Change 0x10 to 0x40
            
            VirtualProtect(pLimit, 4, oldProtect, &oldProtect);
            
            //DevMsg(eDLL_T::ENGINE, "Patched modelprecache limit from 4096 to %d\n", MODEL_PRECACHE_TOTAL_LIMIT);
        }
        else
        {
            // Restore original value on detach
            uint8_t* pLimit = reinterpret_cast<uint8_t*>(s_pModelPrecacheLimitPatch);
            
            DWORD oldProtect;
            VirtualProtect(pLimit, 4, PAGE_EXECUTE_READWRITE, &oldProtect);
            
            // Restore to 0x1000 (4096)
            pLimit[1] = 0x10;
            
            VirtualProtect(pLimit, 4, oldProtect, &oldProtect);
            
            //DevMsg(eDLL_T::ENGINE, "Restored modelprecache limit to 4096\n");
        }
    }

#ifndef CLIENT_DLL
    if (CServer__PrecacheModel)
        DetourSetup(&CServer__PrecacheModel, &CServer_PrecacheModel_Hook, bAttach);
#endif // !CLIENT_DLL
}
