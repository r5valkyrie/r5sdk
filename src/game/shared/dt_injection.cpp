//=============================================================================//
//
// Purpose: DT Field Injection System
//
// Adds new networked entity fields to CPlayer and CWeaponX at runtime by
// expanding their DataTables before the DT decoder builds. The engine's
// networking automatically replicates the injected fields, matching
// SERVER SendProps to CLIENT RecvProps by name.
//
//=============================================================================//

#include "core/stdafx.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "dt_injection.h"

#include <vector>

static std::vector<DTInjectedField> s_playerFields;
static std::vector<DTInjectedField> s_weaponFields;

void DTInject_AddPlayerField(const char* name, SendPropType type, int bits)
{
	DTInjectedField field = {};
	field.name = name;
	field.type = type;
	field.bits = bits;
	field.clientOffset = -1;
	field.serverOffset = -1;
	s_playerFields.push_back(field);
}

void DTInject_AddWeaponField(const char* name, SendPropType type, int bits)
{
	DTInjectedField field = {};
	field.name = name;
	field.type = type;
	field.bits = bits;
	field.clientOffset = -1;
	field.serverOffset = -1;
	s_weaponFields.push_back(field);
}

//-----------------------------------------------------------------------------
static int FindFieldOffset(const std::vector<DTInjectedField>& fields, const char* name, bool server)
{
	for (const auto& f : fields)
	{
		if (strcmp(f.name, name) == 0)
			return server ? f.serverOffset : f.clientOffset;
	}
	return -1;
}

int DTInject_GetPlayerClientOffset(const char* name) { return FindFieldOffset(s_playerFields, name, false); }
int DTInject_GetPlayerServerOffset(const char* name) { return FindFieldOffset(s_playerFields, name, true); }
int DTInject_GetWeaponClientOffset(const char* name) { return FindFieldOffset(s_weaponFields, name, false); }
int DTInject_GetWeaponServerOffset(const char* name) { return FindFieldOffset(s_weaponFields, name, true); }

int DTInject_GetPlayerOffset(SQVM* v, const char* name)
{
	return (v->GetContext() == SQCONTEXT::SERVER)
		? DTInject_GetPlayerServerOffset(name)
		: DTInject_GetPlayerClientOffset(name);
}

int DTInject_GetWeaponOffset(SQVM* v, const char* name)
{
	return (v->GetContext() == SQCONTEXT::SERVER)
		? DTInject_GetWeaponServerOffset(name)
		: DTInject_GetWeaponClientOffset(name);
}

//-----------------------------------------------------------------------------
// CLIENT RecvProp injection (0x68-byte CRecvProp structs, pointer array)
//-----------------------------------------------------------------------------
static constexpr int RECVPROP_SIZE = 0x68;

static void InjectClientProps(void* table, std::vector<DTInjectedField>& fields, int* entitySizePtr)
{
	if (!table || !entitySizePtr || fields.empty())
		return;

	void** pProps = *reinterpret_cast<void***>(reinterpret_cast<uintptr_t>(table) + 0x08);
	int& nProps = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(table) + 0x10);

	const int oldCount = nProps;
	const int addCount = static_cast<int>(fields.size());
	const int newCount = oldCount + addCount;
	const int baseOffset = *entitySizePtr;

	if (oldCount < 0 || oldCount > 4096 || addCount <= 0 || newCount > 4096 || (oldCount > 0 && !pProps))
		return;

	void** newPropPtrs = reinterpret_cast<void**>(malloc(newCount * sizeof(void*)));
	if (!newPropPtrs) return;

	if (pProps)
		memcpy(newPropPtrs, pProps, oldCount * sizeof(void*));

	for (int i = 0; i < addCount; i++)
	{
		uint8_t* prop = reinterpret_cast<uint8_t*>(malloc(RECVPROP_SIZE));
		memset(prop, 0, RECVPROP_SIZE);

		*(int*)(prop + 0x00) = static_cast<int>(fields[i].type);
		*(int*)(prop + 0x04) = baseOffset + i * 4;
		*(int*)(prop + 0x0C) = fields[i].bits;
		*(const char**)(prop + 0x28) = fields[i].name;
		*(int*)(prop + 0x58) = -1;
		*(int*)(prop + 0x5C) = 1;

		fields[i].clientOffset = baseOffset + i * 4;
		newPropPtrs[oldCount + i] = prop;
	}

	*reinterpret_cast<void***>(reinterpret_cast<uintptr_t>(table) + 0x08) = newPropPtrs;
	nProps = newCount;
	*entitySizePtr += addCount * 4;
}

//-----------------------------------------------------------------------------
// SERVER SendProp injection (0x88-byte SendProp structs, contiguous array)
//-----------------------------------------------------------------------------
static constexpr int SENDPROP_SIZE = 0x88;

static void InjectServerProps(void* sendTable, std::vector<DTInjectedField>& fields, int* entitySizePtr)
{
	if (!sendTable || !entitySizePtr || fields.empty())
		return;

	void*& pProps = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(sendTable) + 0x00);
	int& nProps = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(sendTable) + 0x08);

	const int oldCount = nProps;
	const int addCount = static_cast<int>(fields.size());
	const int newCount = oldCount + addCount;
	const int baseOffset = *entitySizePtr;

	if (oldCount < 0 || oldCount > 4096 || addCount <= 0 || newCount > 4096 || (oldCount > 0 && !pProps))
		return;

	uint8_t* newArray = reinterpret_cast<uint8_t*>(malloc(newCount * SENDPROP_SIZE));
	if (!newArray) return;

	if (pProps)
		memcpy(newArray, pProps, oldCount * SENDPROP_SIZE);

	for (int i = 0; i < addCount; i++)
	{
		uint8_t* prop = newArray + (oldCount + i) * SENDPROP_SIZE;
		memset(prop, 0, SENDPROP_SIZE);

		*(int*)(prop + 0x00) = static_cast<int>(fields[i].type);
		*(int*)(prop + 0x04) = fields[i].bits;
		*(void**)(prop + 0x08) = sendTable;
		*(int*)(prop + 0x28) = 1;
		*(int*)(prop + 0x2C) = -1;
		*(const char**)(prop + 0x40) = fields[i].name;
		*(int*)(prop + 0x48) = 4;
		*(uint8_t*)(prop + 0x54) = 0x80;
		*(int*)(prop + 0x58) = 1;
		*(int*)(prop + 0x78) = baseOffset + i * 4;
		*(int*)(prop + 0x80) = -1;

		fields[i].serverOffset = baseOffset + i * 4;
	}

	pProps = newArray;
	nProps = newCount;
	*entitySizePtr += addCount * 4;
}

//-----------------------------------------------------------------------------
// ServerClass linked list traversal — reads server entity sizes.
// Layout: +0x00 name, +0x08 SendTable, +0x10 next, +0x1C entitySize (DT metadata).
// NOTE: entitySize here is DT metadata only. Actual allocation is hardcoded
// in each factory Create function and patched separately.
//-----------------------------------------------------------------------------
static void** g_ppServerClassHead = nullptr;

static void FindServerClassHead()
{
	CMemory stores = Module_FindPattern(g_GameDll,
		"48 89 11 4C 89 41 08 44 89 49 1C");
	if (!stores) return;

	uint8_t* p = reinterpret_cast<uint8_t*>(stores.GetPtr());
	for (int i = 0; i < 80; i++)
	{
		if (p[i] == 0x48 && p[i + 1] == 0x89 && p[i + 2] == 0x1D)
		{
			int32_t disp = *reinterpret_cast<int32_t*>(&p[i + 3]);
			g_ppServerClassHead = reinterpret_cast<void**>(&p[i + 7] + disp);
			return;
		}
	}
}

static int* FindServerEntitySizeForTable(void* sendTable)
{
	if (!g_ppServerClassHead || !*g_ppServerClassHead || !sendTable)
		return nullptr;

	uintptr_t target = reinterpret_cast<uintptr_t>(sendTable);
	void* entry = *g_ppServerClassHead;
	int safety = 0;

	while (entry && safety++ < 2000)
	{
		uintptr_t base = reinterpret_cast<uintptr_t>(entry);
		if (*reinterpret_cast<uintptr_t*>(base + 0x08) == target)
			return reinterpret_cast<int*>(base + 0x1C);
		entry = *reinterpret_cast<void**>(base + 0x10);
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Server entity factory binary patch — patches the hardcoded sizeof(T)
// immediates (alloc + memset) in each factory's Create function.
//-----------------------------------------------------------------------------
static bool PatchServerEntityFactory(int originalSize, int newSize)
{
	if (originalSize == newSize || originalSize <= 0 || newSize <= originalSize)
		return false;

	const uint8_t lo = static_cast<uint8_t>(originalSize & 0xFF);
	const uint8_t hi = static_cast<uint8_t>((originalSize >> 8) & 0xFF);

	const uint8_t pattern[] = {
		0x4C, 0x8B, 0x08,
		0xBA, lo, hi, 0x00, 0x00,
		0x48, 0x8B, 0xC8,
		0x41, 0xFF, 0x51, 0x08,
		0x33, 0xD2,
		0x41, 0xB8, lo, hi, 0x00, 0x00
	};
	const char mask[] = "xxxxxxxxxxxxxxxxxxxxxxx";

	CMemory match = g_GameDll.FindPatternSIMD_Impl(pattern, mask, sizeof(pattern));
	if (!match)
		return false;

	uint8_t* p = reinterpret_cast<uint8_t*>(match.GetPtr());

	DWORD oldProtect;
	VirtualProtect(p, 24, PAGE_EXECUTE_READWRITE, &oldProtect);
	*reinterpret_cast<int*>(p + 4) = newSize;
	*reinterpret_cast<int*>(p + 19) = newSize;
	VirtualProtect(p, 24, oldProtect, &oldProtect);
	return true;
}

//-----------------------------------------------------------------------------
// IDetour implementation
//-----------------------------------------------------------------------------
void VDTInjection::GetFun(void) const
{
	Module_FindPattern(g_GameDll,
		"41 56 48 83 EC 20 65 48 8B 04 25 58 00 00 00 4C 8D 35 ?? ?? ?? ?? BA 40 00 00 00 C7 05")
		.GetPtr(v_ClientWeaponXInit);

	Module_FindPattern(g_GameDll,
		"48 89 4C 24 08 55 53 48 8D AC 24 38 FF FF FF 48 81 EC C8 01 00 00 65 48")
		.GetPtr(v_ClientPlayerInit);

	Module_FindPattern(g_GameDll,
		"40 53 57 41 55 48 83 EC 30 65 48 8B 04 25 58 00 00 00 4C 8D 2D")
		.GetPtr(v_ServerWeaponXInit);

	Module_FindPattern(g_GameDll,
		"40 55 53 48 8D AC 24 58 FF FF FF 48 81 EC A8 01 00 00 65 48 8B 04 25 58")
		.GetPtr(v_ServerPlayerInit);
}

void VDTInjection::GetVar(void) const
{
	if (v_ClientWeaponXInit)
	{
		CMemory fn(reinterpret_cast<uintptr_t>(v_ClientWeaponXInit));
		g_pClientWeaponSize = fn.Offset(0x1B).ResolveRelativeAddress(2, 10).RCast<int*>();
		g_pClientWeaponRecvTable = reinterpret_cast<void*>(
			fn.Offset(0x43).ResolveRelativeAddress(3, 7).GetPtr());
	}

	if (v_ClientPlayerInit)
	{
		CMemory fn(reinterpret_cast<uintptr_t>(v_ClientPlayerInit));
		g_pClientPlayerSize = fn.Offset(0x2B).ResolveRelativeAddress(2, 10).RCast<int*>();
		g_pClientPlayerRecvTable = reinterpret_cast<void*>(
			fn.Offset(0x53).ResolveRelativeAddress(3, 7).GetPtr());
	}

	if (v_ServerWeaponXInit)
	{
		CMemory fn(reinterpret_cast<uintptr_t>(v_ServerWeaponXInit));
		g_pServerWeaponSendTable = reinterpret_cast<void*>(
			fn.Offset(0x39).ResolveRelativeAddress(3, 7).GetPtr());
	}

	if (v_ServerPlayerInit)
	{
		CMemory fn(reinterpret_cast<uintptr_t>(v_ServerPlayerInit));
		g_pServerPlayerSendTable = reinterpret_cast<void*>(
			fn.Offset(0x45).ResolveRelativeAddress(3, 7).GetPtr());
	}

	FindServerClassHead();
	g_pServerWeaponSize = FindServerEntitySizeForTable(
		reinterpret_cast<void*>(g_pServerWeaponSendTable));
	g_pServerPlayerSize = FindServerEntitySizeForTable(
		reinterpret_cast<void*>(g_pServerPlayerSendTable));

	// Edict dirty-marking global (RVA 0xD4EC368)
	g_nServerStateAddr = g_GameDll.GetModuleBase() + 0xD4EC368;

	DTInject_AddWeaponField("m_infiniteAmmoState", SendPropType::DPT_Int, 10);
	DTInject_AddWeaponField("m_weaponLockedSet", SendPropType::DPT_Int, 10);
	DTInject_AddPlayerField("m_extraShieldHealth", SendPropType::DPT_Int, 32);
	DTInject_AddPlayerField("m_extraShieldTier", SendPropType::DPT_Int, 10);
	DTInject_AddPlayerField("m_phaseShiftType", SendPropType::DPT_Int, 10);
	DTInject_AddPlayerField("m_deathFieldIndex", SendPropType::DPT_Int, 10);
}

void VDTInjection::Detour(const bool bAttach) const
{
	if (!bAttach)
		return;

	// --- Weapon entity ---
	if (!s_weaponFields.empty())
	{
		const int clientSize = g_pClientWeaponSize ? *g_pClientWeaponSize : 0;
		const int serverSize = g_pServerWeaponSize ? *g_pServerWeaponSize : 0;
		const int fieldBytes = static_cast<int>(s_weaponFields.size()) * 4;

		if (clientSize > 0 && g_pClientWeaponRecvTable && g_pClientWeaponSize)
		{
			PatchServerEntityFactory(clientSize, clientSize + fieldBytes);
			InjectClientProps(g_pClientWeaponRecvTable, s_weaponFields, g_pClientWeaponSize);
		}

		if (serverSize > 0 && g_pServerWeaponSendTable && g_pServerWeaponSize)
		{
			PatchServerEntityFactory(serverSize, serverSize + fieldBytes);
			InjectServerProps(g_pServerWeaponSendTable, s_weaponFields, g_pServerWeaponSize);
		}
	}

	// --- Player entity ---
	if (!s_playerFields.empty())
	{
		const int clientSize = g_pClientPlayerSize ? *g_pClientPlayerSize : 0;
		const int serverSize = g_pServerPlayerSize ? *g_pServerPlayerSize : 0;
		const int fieldBytes = static_cast<int>(s_playerFields.size()) * 4;

		if (clientSize > 0 && g_pClientPlayerRecvTable && g_pClientPlayerSize)
		{
			PatchServerEntityFactory(clientSize, clientSize + fieldBytes);
			InjectClientProps(g_pClientPlayerRecvTable, s_playerFields, g_pClientPlayerSize);
		}

		if (serverSize > 0 && g_pServerPlayerSendTable && g_pServerPlayerSize)
		{
			PatchServerEntityFactory(serverSize, serverSize + fieldBytes);
			InjectServerProps(g_pServerPlayerSendTable, s_playerFields, g_pServerPlayerSize);
		}
	}

	DevMsg(eDLL_T::ENGINE, "[DT_Inject] Injected %d weapon + %d player networked fields\n",
		(int)s_weaponFields.size(), (int)s_playerFields.size());
}
