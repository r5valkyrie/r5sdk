#ifndef SCRIPTREMOTEFUNCTIONS_SHARED_H
#define SCRIPTREMOTEFUNCTIONS_SHARED_H

constexpr int SCRIPT_REMOTE_SERVER_MAX_PARAMS = 8;
constexpr int SCRIPT_REMOTE_SERVER_MAX_STRING_LEN = 255;
constexpr int SCRIPT_REMOTE_SERVER_MAX_FUNCTIONS = 768;
constexpr int SCRIPT_REMOTE_FUNC_INDEX_BITS = 10;

// Remote function parameter type byte values
enum class ScriptRemoteParamType_e : uint8_t
{
	SRP_FLOAT         = 1,
	SRP_VECTOR        = 3,
	SRP_INT           = 5,
	SRP_BOOL          = 6,
	SRP_ENTITY        = 40,
	SRP_TYPED_ENTITY  = 41,
	SRP_ITEMFLAVOR    = 42,
};

struct ScriptRemoteParamDesc_t
{
	ScriptRemoteParamType_e type;
	int intMin;
	int intMax;
	float floatMin;
	float floatMax;
	int floatBits;
};

struct ScriptRemoteFuncDesc_t
{
	char szName[128];
	uint16_t nIndex;
	int nParamCount;
	ScriptRemoteParamDesc_t params[SCRIPT_REMOTE_SERVER_MAX_PARAMS];
};

bool ScriptRemoteServer_RegisterFunction(const char* pszName, int nParamCount,
	const ScriptRemoteParamDesc_t* pParams);
const ScriptRemoteFuncDesc_t* ScriptRemoteServer_FindFunction(const char* pszName);
const ScriptRemoteFuncDesc_t* ScriptRemoteServer_GetFunctionByIndex(uint16_t nIndex);
int ScriptRemoteServer_GetFunctionCount();
uint32_t ScriptRemoteServer_CalcChecksum();
void ScriptRemoteServer_LockRegistrations();
void ScriptRemoteServer_ClearRegistrations();

#endif // SCRIPTREMOTEFUNCTIONS_SHARED_H
