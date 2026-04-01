#pragma once
#include "mathlib/vector.h"

//-----------------------------------------------------------------------------
// CellAABBNode structure (32 bytes) - 0x77 lump
//-----------------------------------------------------------------------------
#pragma pack(push, 1)
struct dcellaabbnode_t
{
    Vector3D mins;              // +0x00: 12 bytes
    uint8_t  childCount;        // +0x0C: 1 byte
    uint16_t firstChild;        // +0x0D: 2 bytes
    uint8_t  childFlags;        // +0x0F: 1 byte
    Vector3D maxs;              // +0x10: 12 bytes
    uint8_t  objRefCount;       // +0x1C: 1 byte
    uint16_t objRefOffset;      // +0x1D: 2 bytes
    uint8_t  objRefFlags;       // +0x1F: 1 byte
};
#pragma pack(pop)
static_assert(sizeof(dcellaabbnode_t) == 32, "dcellaabbnode_t must be 32 bytes");

//-----------------------------------------------------------------------------
// CellBSPNode structure (8 bytes) - 0x6A lump
//-----------------------------------------------------------------------------
struct dcellbspnode_t
{
    int32_t planeNum;           // Plane index, -1 for leaf
    int32_t childrenOrCell;     // If leaf: cell index, else: children indices packed
};
static_assert(sizeof(dcellbspnode_t) == 8, "dcellbspnode_t must be 8 bytes");

//-----------------------------------------------------------------------------
// Cell structure (8 bytes) - 0x6B lump
//-----------------------------------------------------------------------------
struct dcell_t
{
    int32_t aabbIndex;          // Index into CellAABBNodes
    int32_t flags;              // Flags (usually 0xFFFF0000)
};
static_assert(sizeof(dcell_t) == 8, "dcell_t must be 8 bytes");

//-----------------------------------------------------------------------------
// ObjReferenceBounds structure (24 bytes) - 0x79 lump
// Just 6 floats: mins + maxs, no padding
//-----------------------------------------------------------------------------
struct dobjrefbounds_t
{
    Vector3D mins;              // +0x00: 12 bytes
    Vector3D maxs;              // +0x0C: 12 bytes
};
static_assert(sizeof(dobjrefbounds_t) == 24, "dobjrefbounds_t must be 24 bytes");

//-----------------------------------------------------------------------------
// Validation functions
//-----------------------------------------------------------------------------
bool ValidateCellAABBNodes(const dcellaabbnode_t* nodes, int nodeCount, int objRefCount);
bool ValidateCells(const dcell_t* cells, int cellCount, int aabbNodeCount);
void DumpVisTreeInfo();

///////////////////////////////////////////////////////////////////////////////
class VVisDebug : public IDetour
{
    virtual void GetAdr(void) const { }
    virtual void GetFun(void) const { }
    virtual void GetVar(void) const { }
    virtual void GetCon(void) const { }
    virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////
