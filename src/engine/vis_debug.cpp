//=============================================================================//
//
// Purpose: BSP Visibility Tree Debugging
// 
// This file adds debugging and validation for BSP visibility data structures
// to help catch malformed data before it causes crashes.
//
//=============================================================================//

#include "core/stdafx.h"
#include "tier1/cvar.h"
#include "engine/vis_debug.h"

static ConVar vis_debug_enabled("vis_debug_enabled", "1", FCVAR_DEVELOPMENTONLY, "Enable visibility tree debugging");
static ConVar vis_debug_verbose("vis_debug_verbose", "0", FCVAR_DEVELOPMENTONLY, "Verbose visibility tree debugging");

//-----------------------------------------------------------------------------
// Purpose: Validate CellAABBNode data after loading
// Input  : *nodes - pointer to node array
//          nodeCount - number of nodes
//          objRefCount - total number of object references
// Output : true if valid
//-----------------------------------------------------------------------------
bool ValidateCellAABBNodes(const dcellaabbnode_t* nodes, int nodeCount, int objRefCount)
{
    if (!vis_debug_enabled.GetBool())
        return true;

    if (!nodes || nodeCount <= 0)
    {
        Warning(eDLL_T::ENGINE, "ValidateCellAABBNodes: No nodes to validate\n");
        return true;
    }

    DevMsg(eDLL_T::ENGINE, "ValidateCellAABBNodes: Validating %d nodes, %d objRefs\n", nodeCount, objRefCount);

    bool valid = true;
    for (int i = 0; i < nodeCount; i++)
    {
        const dcellaabbnode_t& node = nodes[i];

        // Validate firstChild
        if (node.childCount > 0 && node.firstChild >= nodeCount)
        {
            Warning(eDLL_T::ENGINE, "CellAABBNode[%d]: Invalid firstChild=%d (max=%d), childCount=%d\n",
                i, node.firstChild, nodeCount - 1, node.childCount);
            valid = false;
        }

        // Validate objRefOffset
        if (node.objRefCount > 0 && node.objRefOffset + node.objRefCount > objRefCount)
        {
            Warning(eDLL_T::ENGINE, "CellAABBNode[%d]: Invalid objRefOffset=%d + objRefCount=%d > %d\n",
                i, node.objRefOffset, node.objRefCount, objRefCount);
            valid = false;
        }

        // Validate bounds - check for obviously wrong values (like float values misinterpreted)
        if (node.mins.x < -100000.0f || node.mins.x > 100000.0f ||
            node.mins.y < -100000.0f || node.mins.y > 100000.0f ||
            node.mins.z < -100000.0f || node.mins.z > 100000.0f)
        {
            Warning(eDLL_T::ENGINE, "CellAABBNode[%d]: Suspicious mins values (%.1f, %.1f, %.1f)\n",
                i, node.mins.x, node.mins.y, node.mins.z);
        }

        if (vis_debug_verbose.GetBool())
        {
            DevMsg(eDLL_T::ENGINE, "  Node[%d]: firstChild=%d, childCount=%d, childFlags=0x%02X, "
                "objRefOffset=%d, objRefCount=%d, objRefFlags=0x%02X\n",
                i, node.firstChild, node.childCount, node.childFlags,
                node.objRefOffset, node.objRefCount, node.objRefFlags);
        }
    }

    if (!valid)
    {
        Error(eDLL_T::ENGINE, NO_ERROR, "CellAABBNode validation failed! BSP visibility data may be corrupt.\n");
    }

    return valid;
}

//-----------------------------------------------------------------------------
// Purpose: Validate Cell data after loading
// Input  : *cells - pointer to cell array
//          cellCount - number of cells
//          aabbNodeCount - total number of AABB nodes
// Output : true if valid
//-----------------------------------------------------------------------------
bool ValidateCells(const dcell_t* cells, int cellCount, int aabbNodeCount)
{
    if (!vis_debug_enabled.GetBool())
        return true;

    if (!cells || cellCount <= 0)
        return true;

    DevMsg(eDLL_T::ENGINE, "ValidateCells: Validating %d cells\n", cellCount);

    bool valid = true;
    for (int i = 0; i < cellCount; i++)
    {
        const dcell_t& cell = cells[i];

        // Validate aabbIndex
        if (cell.aabbIndex < 0 || cell.aabbIndex >= aabbNodeCount)
        {
            Warning(eDLL_T::ENGINE, "Cell[%d]: Invalid aabbIndex=%d (max=%d)\n",
                i, cell.aabbIndex, aabbNodeCount - 1);
            valid = false;
        }

        if (vis_debug_verbose.GetBool())
        {
            DevMsg(eDLL_T::ENGINE, "  Cell[%d]: aabbIndex=%d, flags=0x%08X\n",
                i, cell.aabbIndex, cell.flags);
        }
    }

    return valid;
}

//-----------------------------------------------------------------------------
// Purpose: Dump vis tree info for debugging
//-----------------------------------------------------------------------------
void DumpVisTreeInfo()
{
    // This would be called after loading to dump the visibility tree structure
    // Requires access to the loaded BSP data which we'd need to hook
    DevMsg(eDLL_T::ENGINE, "DumpVisTreeInfo: Not yet implemented - need BSP load hook\n");
}

void VVisDebug::Detour(const bool bAttach) const
{
    // TODO: Add detours for BSP loading functions to validate vis data
    // Could hook CMapLoadHelper::CMapLoadHelper to intercept lump loading
}
