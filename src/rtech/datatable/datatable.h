#ifndef RTECH_DATATABLE_H
#define RTECH_DATATABLE_H
#include "thirdparty/detours/include/idetour.h"

//-----------------------------------------------------------------------------
// Column types
//-----------------------------------------------------------------------------
enum DatatableColumnType_e : int
{
	DTCOL_BOOL = 0,
	DTCOL_INT = 1,
	DTCOL_FLOAT = 2,
	DTCOL_VECTOR = 3,
	DTCOL_STRING = 4,
	DTCOL_ASSET = 5,
	DTCOL_ASSET_NOPRECACHE = 6,
	DTCOL_COUNT
};

//-----------------------------------------------------------------------------
// Engine structures
//-----------------------------------------------------------------------------
struct DTColumnData
{
	const char* name;
	int         type;
	int         byteOffset;
};
static_assert(sizeof(DTColumnData) == 16, "DTColumnData size mismatch");

struct DataTableHeader
{
	int             columnCount;
	int             rowCount;
	DTColumnData*   columnData;
	char*           rows;
	uint64_t        dataTableCRC;
	int             rowSize;
	char            pad[4];
};
static_assert(sizeof(DataTableHeader) == 40, "DataTableHeader size mismatch");

struct DiskDatatable
{
	DataTableHeader header;
	uint64_t        guid;
	char*           memBlock;
	size_t          memBlockSize;
	bool            insertedInHashTable;
	char            rpakPath[256];
};

//-----------------------------------------------------------------------------
// Engine functions
//-----------------------------------------------------------------------------
inline __int64(*v_Script_GetDatatable)(__int64 sqvm);
inline __int64(*v_Script_GetDatatableRowCount)(__int64 sqvm);
inline uint64_t(*v_HashNameAligned)(const char* name);
inline uint64_t(*v_HashNameUnaligned)(const char* name);

//-----------------------------------------------------------------------------
// Engine globals
//-----------------------------------------------------------------------------
inline void* g_pPakAssetHashTable;
inline uint64_t* g_pDatatableGuidCache;

///////////////////////////////////////////////////////////////////////////////
class V_Datatable : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("Script_GetDatatable", v_Script_GetDatatable);
		LogFunAdr("Script_GetDatatableRowCount", v_Script_GetDatatableRowCount);
		LogFunAdr("HashNameAligned", v_HashNameAligned);
		LogFunAdr("HashNameUnaligned", v_HashNameUnaligned);
		LogVarAdr("g_pPakAssetHashTable", g_pPakAssetHashTable);
		LogVarAdr("g_pDatatableGuidCache", g_pDatatableGuidCache);
	}
	virtual void GetFun(void) const
	{
		v_Script_GetDatatable = reinterpret_cast<decltype(v_Script_GetDatatable)>(
			g_GameDll.GetModuleBase() + 0x7F19A0);
		v_Script_GetDatatableRowCount = reinterpret_cast<decltype(v_Script_GetDatatableRowCount)>(
			g_GameDll.GetModuleBase() + 0x842070);
		v_HashNameAligned = reinterpret_cast<decltype(v_HashNameAligned)>(
			g_GameDll.GetModuleBase() + 0x46CD80);
		v_HashNameUnaligned = reinterpret_cast<decltype(v_HashNameUnaligned)>(
			g_GameDll.GetModuleBase() + 0x46CEA0);
	}
	virtual void GetVar(void) const
	{
		CMemory pakFind(g_GameDll.GetModuleBase() + 0x442172);
		g_pPakAssetHashTable = pakFind.ResolveRelativeAddressSelf(0x3, 0x7).RCast<void*>();

		CMemory guidCache(g_GameDll.GetModuleBase() + 0x7F1BDF);
		g_pDatatableGuidCache = guidCache.ResolveRelativeAddressSelf(0x3, 0x7).RCast<uint64_t*>();
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // RTECH_DATATABLE_H
