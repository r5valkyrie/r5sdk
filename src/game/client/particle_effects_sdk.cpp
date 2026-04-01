#include "core/stdafx.h"
#include "game/shared/vscript_gamedll_defs.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "vscript/languages/squirrel_re/include/squirrel.h"
#include "vscript/languages/squirrel_re/vsquirrel.h"
#include "vscript_client.h"
#include "particle_effects_sdk.h"

static CRITICAL_SECTION* s_pParticleMutex = nullptr;
static int*              s_pParticleHandles = nullptr;
static constexpr int     PARTICLE_HANDLE_MAX = 0x4B0;
static constexpr int     PARTICLE_HANDLE_ENTRY_DWORDS = 10;
static constexpr int     PARTICLE_OBJECT_DWORD_OFFSET = 8;

typedef void (__fastcall *ParticleRestartFn_t)(void* pCollection, bool bSleep);
static ParticleRestartFn_t s_fnParticleRestart = nullptr;

static bool s_bInitDone = false;

static void ParticleEffects_SDK_Init()
{
	if (s_bInitDone)
		return;
	s_bInitDone = true;

	CMemory effectFuncMem = Module_FindPattern(g_GameDll,
		"48 89 5C 24 08 57 48 83 EC 20 8B D9 0F B6 FA 48 8D 0D");

	if (effectFuncMem)
	{
		s_pParticleMutex = effectFuncMem.Offset(0x0F)
			.ResolveRelativeAddress(0x3, 0x7)
			.RCast<CRITICAL_SECTION*>();

		CMemory handleArrayRef = effectFuncMem
			.FindPatternSelf("48 8D 05", CMemory::Direction::DOWN, 100);

		if (handleArrayRef)
		{
			s_pParticleHandles = handleArrayRef
				.ResolveRelativeAddress(0x3, 0x7)
				.RCast<int*>();
		}
	}

	CMemory restartMem = Module_FindPattern(g_GameDll,
		"48 89 5C 24 08 57 48 83 EC 30 0F B6 FA 48 8B D9 84 D2 74 0D 80 B9 A8 00 00 00 00");

	if (restartMem)
		restartMem.GetPtr(s_fnParticleRestart);
}

static void* ValidateEffectHandle(HSQUIRRELVM v, SQInteger nHandle)
{
	if (nHandle == -1)
		return nullptr;

	if (nHandle < 0 || nHandle > 0xFFFF)
	{
		v_SQVM_RaiseError(v, "No existing effect for handle 0x%x.\n",
			static_cast<int>(nHandle & 0xFFFFFFFF));
		return nullptr;
	}

	const unsigned short idx = static_cast<unsigned short>(nHandle);

	if (idx >= PARTICLE_HANDLE_MAX)
	{
		v_SQVM_RaiseError(v, "No existing effect for handle 0x%x.\n",
			static_cast<int>(nHandle));
		return nullptr;
	}

	if (!s_pParticleHandles)
		return nullptr;

	const int nEntryOffset = PARTICLE_HANDLE_ENTRY_DWORDS * idx;

	if (s_pParticleHandles[nEntryOffset] != static_cast<int>(nHandle))
	{
		v_SQVM_RaiseError(v, "No existing effect for handle 0x%x.\n",
			static_cast<int>(nHandle));
		return nullptr;
	}

	void* pObject = *reinterpret_cast<void**>(
		&s_pParticleHandles[nEntryOffset + PARTICLE_OBJECT_DWORD_OFFSET]);

	if (!pObject)
		return nullptr;

	return pObject;
}

static SQRESULT ClientScript_EffectRestart(HSQUIRRELVM v)
{
	SQInteger nHandle = 0;
	SQInteger nResetMode = 0;
	SQInteger nKillEffects = 0;

	sq_getinteger(v, 2, &nHandle);
	sq_getinteger(v, 3, &nResetMode);
	sq_getinteger(v, 4, &nKillEffects);

	if (!s_pParticleMutex || !s_fnParticleRestart)
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

	EnterCriticalSection(s_pParticleMutex);

	void* pObject = ValidateEffectHandle(v, nHandle);
	if (pObject)
	{
		if (nKillEffects)
			s_fnParticleRestart(pObject, true);

		s_fnParticleRestart(pObject, false);
	}

	LeaveCriticalSection(s_pParticleMutex);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT ClientScript_EffectSetDistanceCullingScalar(HSQUIRRELVM v)
{
	SQInteger nHandle = 0;
	SQFloat flScalar = 1.0f;

	sq_getinteger(v, 2, &nHandle);
	sq_getfloat(v, 3, &flScalar);

	if (!s_pParticleMutex)
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

	EnterCriticalSection(s_pParticleMutex);
	ValidateEffectHandle(v, nHandle);
	LeaveCriticalSection(s_pParticleMutex);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

void ParticleEffects_SDK_RegisterClientFunctions(CSquirrelVM* vm)
{
	if (!vm)
		return;

	ParticleEffects_SDK_Init();

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(vm, EffectRestart,
		"Restarts a particle effect",
		"void", "int effectHandle, bool resetAndMakeSureEmitsHappen, bool shouldKillEffects",
		false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(vm, EffectSetDistanceCullingScalar,
		"Sets the distance culling scalar for a particle effect",
		"void", "int effectHandle, float distanceScalar",
		false);
}
