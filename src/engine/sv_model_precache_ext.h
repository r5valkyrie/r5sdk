//=============================================================================//
//
// Purpose: Extended model precache system to support more than 4096 models
// 
// This system extends the model precache limit from 4096 to 16384 by providing
// additional storage for model indices >= 4096 and hooking the relevant
// GetModel/PrecacheModel functions to redirect lookups to extended storage.
//
// Components:
// - Extended Storage: g_ExtendedServerModelPrecache[12288] for indices 4096-16383 (server)
//                     g_ExtendedClientModelPrecache[12288] for indices 4096-16383 (client)
// - Hook: CServer::GetModel - Redirects model lookups >= 4096 to extended storage
// - Hook: CClientState::GetModel - Redirects client model lookups >= 4096 to extended storage
// - Hook: CServer::PrecacheModel - Stores models with index >= 4096 in extended storage
// - Hook: Entity GetSequenceData - Prevents crash when animation data is null for extended models
//
//=============================================================================//

#ifndef SV_MODEL_PRECACHE_EXT_H
#define SV_MODEL_PRECACHE_EXT_H

#include "gl_model_private.h"

//-----------------------------------------------------------------------------
// Extended precache storage limits
//-----------------------------------------------------------------------------
constexpr int MODEL_PRECACHE_BASE_LIMIT = 4096;     // Original engine limit
constexpr int MODEL_PRECACHE_EXTENDED_COUNT = 12288; // Extended storage size (4096-16383)
constexpr int MODEL_PRECACHE_TOTAL_LIMIT = MODEL_PRECACHE_BASE_LIMIT + MODEL_PRECACHE_EXTENDED_COUNT; // 16384

//-----------------------------------------------------------------------------
// CPrecacheItem - Represents a precached resource
//-----------------------------------------------------------------------------
struct CPrecacheItem
{
	enum PrecacheType
	{
		TYPE_MODEL = 1,
		TYPE_GENERIC = 2,
		TYPE_SOUND = 3,
		TYPE_DECAL = 4,
	};

	inline model_t* GetModel() const 
	{ 
		if (!u.model)
			return nullptr;
		return u.model; 
	}

	inline void SetModel(const model_t* pModel)
	{
		u.model = const_cast<model_t*>(pModel);
		m_nFlags = (m_nFlags & ~7) | TYPE_MODEL;
	}

	inline void Clear()
	{
		m_nFlags = 0;
		u.model = nullptr;
	}

	int m_nFlags;
	int m_nPad;
	union
	{
		model_t* model;
		void* generic;
	} u;
};
static_assert(sizeof(CPrecacheItem) == 16, "CPrecacheItem size mismatch");

//-----------------------------------------------------------------------------
// Extended storage for models with indices >= 4096
//-----------------------------------------------------------------------------
#ifndef CLIENT_DLL
extern CPrecacheItem g_ExtendedServerModelPrecache[MODEL_PRECACHE_EXTENDED_COUNT];
#endif // !CLIENT_DLL

#ifndef DEDICATED
extern CPrecacheItem g_ExtendedClientModelPrecache[MODEL_PRECACHE_EXTENDED_COUNT];
#endif // !DEDICATED

//-----------------------------------------------------------------------------
// Helper functions
//-----------------------------------------------------------------------------
inline bool IsExtendedModelIndex(int index)
{
	return index >= MODEL_PRECACHE_BASE_LIMIT && index < MODEL_PRECACHE_TOTAL_LIMIT;
}

inline int GetExtendedStorageIndex(int modelIndex)
{
	return modelIndex - MODEL_PRECACHE_BASE_LIMIT;
}

//-----------------------------------------------------------------------------
// Hook functions
//-----------------------------------------------------------------------------
#ifndef CLIENT_DLL
class CServer;

// Server-side precache hook - stores models with index >= 4096 in extended storage
int CServer_PrecacheModel_Hook(CServer* thisptr, const char* szModelName);
#endif // !CLIENT_DLL

//-----------------------------------------------------------------------------
// Function pointers for original functions
//-----------------------------------------------------------------------------
#ifndef CLIENT_DLL
inline int (*CServer__PrecacheModel)(CServer* thisptr, const char* szModelName);
#endif // !CLIENT_DLL

//-----------------------------------------------------------------------------
// Initialization
//-----------------------------------------------------------------------------
void SV_ModelPrecacheExt_Init();
void SV_ModelPrecacheExt_Shutdown();
void SV_ModelPrecacheExt_ClearServer();
void SV_ModelPrecacheExt_ClearClient();

///////////////////////////////////////////////////////////////////////////////
class VSV_ModelPrecacheExt : public IDetour
{
	virtual void GetAdr(void) const
	{
#ifndef CLIENT_DLL
		LogFunAdr("CServer::PrecacheModel", CServer__PrecacheModel);
#endif // !CLIENT_DLL
	}
	virtual void GetFun(void) const;
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // SV_MODEL_PRECACHE_EXT_H
