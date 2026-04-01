//===============================================================================//
//
// Purpose: VSquirrel VM
//
//===============================================================================//
#include "tier0/fasttimer.h"
#include "vscript/vscript.h"
#include "pluginsystem/modsystem.h"
#include "sqclosure.h"
#include "sqfuncproto.h"
#include "sqstring.h"
#include "vsquirrel.h"

//---------------------------------------------------------------------------------
// Console variables
//---------------------------------------------------------------------------------
static ConVar script_profile_codecalls("script_profile_codecalls", "0", FCVAR_DEVELOPMENTONLY, "Prints duration of native calls to script functions.", "0 = none, 1 = slow calls, 2 = all ( !slower! )");
static ConVar script_show_output("script_show_output", "0", FCVAR_RELEASE, "Prints the VM output to the console ( !slower! ).", true, 0.f, true, 2.f, "0 = log to file. 1 = 0 + log to console. 2 = 1 + log to notify");
static ConVar script_show_warning("script_show_warning", "0", FCVAR_RELEASE, "Prints the VM warning output to the console ( !slower! ).", true, 0.f, true, 2.f, "0 = log to file. 1 = 0 + log to console. 2 = 1 + log to notify");

// Callbacks for registering abstracted script functions.
void(*ServerScriptRegister_Callback)(CSquirrelVM* const s) = nullptr;
void(*ClientScriptRegister_Callback)(CSquirrelVM* const s) = nullptr;
void(*UiScriptRegister_Callback)(CSquirrelVM* const s) = nullptr;

// Callbacks for registering script enums.
void(*ServerScriptRegisterEnum_Callback)(CSquirrelVM* const s) = nullptr;
void(*ClientScriptRegisterEnum_Callback)(CSquirrelVM* const s) = nullptr;
void(*UIScriptRegisterEnum_Callback)(CSquirrelVM* const s) = nullptr;

// Admin panel functions, NULL on dedicated and client only builds.
void(*UiServerScriptRegister_Callback)(CSquirrelVM* const s) = nullptr;
void(*UiAdminPanelScriptRegister_Callback)(CSquirrelVM* const s) = nullptr;

// Registering constants in scripts.
void(*ScriptConstantRegister_Callback)(CSquirrelVM* const s) = nullptr;

//---------------------------------------------------------------------------------
// Purpose: Initialises a Squirrel VM instance
// Output : True on success, false on failure
//---------------------------------------------------------------------------------
bool CSquirrelVM::Init(CSquirrelVM* s, SQCONTEXT context, SQFloat curTime)
{
	// original func always returns true, added check just in case.
	if (!CSquirrelVM__Init(s, context, curTime))
	{
		return false;
	}

	Msg((eDLL_T)context, "Created %s VM: '0x%p'\n", s->GetVM()->_sharedstate->_contextname, s);

	switch (context)
	{
	case SQCONTEXT::SERVER:
		g_pServerScript = s;

		if (ServerScriptRegister_Callback)
			ServerScriptRegister_Callback(s);

		break;
	case SQCONTEXT::CLIENT:
		g_pClientScript = s;

		if (ClientScriptRegister_Callback)
			ClientScriptRegister_Callback(s);

		break;
	case SQCONTEXT::UI:
		g_pUIScript = s;

		if (UiScriptRegister_Callback)
			UiScriptRegister_Callback(s);

		if (UiServerScriptRegister_Callback)
			UiServerScriptRegister_Callback(s);

		if (UiAdminPanelScriptRegister_Callback)
			UiAdminPanelScriptRegister_Callback(s);

		break;
	}

	return true;
}

//---------------------------------------------------------------------------------
// Purpose: destroys the signal entry list head
// Input  : *s - 
//			v - 
//			f - 
// Output : true on success, false otherwise
//---------------------------------------------------------------------------------
bool CSquirrelVM::DestroySignalEntryListHead(CSquirrelVM* s, HSQUIRRELVM v, SQFloat f)
{
	SQBool result = CSquirrelVM__DestroySignalEntryListHead(s, v, f);
	s->RegisterConstant("DEVELOPER", developer->GetInt());

	// Must have one.
	Assert(ScriptConstantRegister_Callback);
	ScriptConstantRegister_Callback(s);

	return result;
}

//---------------------------------------------------------------------------------
// Purpose: registers a global constant
// Input  : *name - 
//			value - 
//---------------------------------------------------------------------------------
SQRESULT CSquirrelVM::RegisterConstant(const SQChar* name, SQInteger value)
{
	return CSquirrelVM__RegisterConstant(this, name, value);
}

//---------------------------------------------------------------------------------
// Purpose: runs text as script on the VM
// Input  : *script - 
// Output : true on success, false otherwise
//---------------------------------------------------------------------------------
bool CSquirrelVM::Run(const SQChar* const script)
{
	Assert(m_hVM);

	bool success = false;
	SQBufState bufState(script);

	if (SQ_SUCCEEDED(sq_compilebuffer(m_hVM, &bufState, "unnamed", -1, SQTrue)))
	{
		SQObject hScript;
		sq_getstackobj(m_hVM, -1, &hScript);

		sq_addref(m_hVM, &hScript);
		sq_pop(m_hVM, 1);

		if (ExecuteFunction((HSCRIPT)&hScript, NULL, 0, NULL, NULL) == SCRIPT_DONE)
			success = true;

		sq_release(m_hVM, &hScript);
	}

	return success;
}

//---------------------------------------------------------------------------------
// Purpose: executes a function by handle
// Input  : hFunction - 
//			*pArgs - 
//			nArgs - 
//			*pReturn - 
//			hScope - 
// Output : SCRIPT_DONE on success, SCRIPT_ERROR otherwise
//---------------------------------------------------------------------------------
ScriptStatus_t CSquirrelVM::ExecuteFunction(HSCRIPT hFunction, const ScriptVariant_t* const pArgs, unsigned int nArgs, ScriptVariant_t* const pReturn, HSCRIPT hScope)
{
	const SQObjectPtr* const f = reinterpret_cast<SQObjectPtr*>(hFunction);

	// Only script closures (OT_CLOSURE) have a SQFunctionProto with profiling
	// metadata. Native closures (OT_NATIVECLOSURE) have a different layout and
	// accessing _function on them reads garbage, causing a crash.
	const SQFunctionProto* fp = nullptr;
	const char* functionName = "(native)";

	if (sq_isclosure(*f))
	{
		const SQClosure* const closure = _closure(*f);
		fp = _funcproto(closure->_function);

		if (fp)
			functionName = _stringval(fp->_funcname);
	}

	const bool hasFuncProto = fp != nullptr;
	CFastTimer callTimer;

	// Start a timer for any named function call
	if (hasFuncProto)
		callTimer.Start();

	// NOTE: pArgs and pReturn are most likely of type 'ScriptVariant_t', needs to be reversed.
	const ScriptStatus_t result = CSquirrelVM__ExecuteFunction(this, hFunction, pArgs, nArgs, pReturn, hScope);

	if (hasFuncProto)
	{
		// End the timer as soon as possible after the call has completed to make sure the time is accurate.
		callTimer.End();

		const int printMode = script_profile_codecalls.GetInt();

		// If print mode is not "none"
		if (printMode > 0)
		{
			const double durationMS = callTimer.GetDuration().GetMillisecondsF();

			// If printMode is set to "all", or the duration is greater than or equal to one millisecond.
			if (printMode == 2 || (printMode == 1 && durationMS >= 1.f))
				Msg(this->GetNativeContext(), "Script function '%s' took %.3fms\n", functionName, durationMS);
		}
	}

	return result;
}

ScriptStatus_t Script_ExecuteFunction(CSquirrelVM* s, HSCRIPT hFunction, const ScriptVariant_t* const pArgs, unsigned int nArgs, ScriptVariant_t* const pReturn, HSCRIPT hScope)
{
	return s->ExecuteFunction(hFunction, pArgs, nArgs, pReturn, hScope);
}

//---------------------------------------------------------------------------------
// Purpose: executes a code callback
// Input  : *name - 
// Output : true on success, false otherwise
//---------------------------------------------------------------------------------
bool CSquirrelVM::ExecuteCodeCallback(const SQChar* const name)
{
	return CSquirrelVM__ExecuteCodeCallback(this, name);
}

//---------------------------------------------------------------------------------
// Purpose: registers a code function
// Input  : *binding - 
//			useTypeCompiler - 
//---------------------------------------------------------------------------------
SQRESULT CSquirrelVM::RegisterFunction(ScriptFunctionBinding_t* const binding, const bool useTypeCompiler)
{
	SQRESULT results = CSquirrelVM__RegisterFunction(this, binding, useTypeCompiler);
	return results;
}

//---------------------------------------------------------------------------------
// Purpose: Finds a function in the squirrel VM
// Input  : *pszFunctionName - 
//			*pszFunctionSig - 
//			 hScope - 
// Output: Function handle on success NULL on failure
//---------------------------------------------------------------------------------
const HSCRIPT CSquirrelVM::FindFunction(const char* const pszFunctionName, const char* const pszFunctionSig, HSCRIPT hScope)
{
	return CSquirrelVM__FindFunction(this, pszFunctionName, pszFunctionSig, hScope);
}

//---------------------------------------------------------------------------------
// Purpose: sets current VM as the global precompiler
// Input  : *name - 
//			value - 
//---------------------------------------------------------------------------------
void CSquirrelVM::SetAsCompiler(RSON::Node_t* rson)
{
	const SQCONTEXT context = GetContext();
	switch (context)
	{
	case SQCONTEXT::SERVER:
	{
		v_Script_SetServerPrecompiler(context, rson);
		break;
	}
	case SQCONTEXT::CLIENT:
	case SQCONTEXT::UI:
	{
		v_Script_SetClientPrecompiler(context, rson);
		break;
	}
	}
}

//---------------------------------------------------------------------------------
// Purpose: prints the output of each VM to the console
// Input  : *sqvm - 
//			*fmt - 
//			... - 
//---------------------------------------------------------------------------------
SQRESULT Script_PrintFunc(HSQUIRRELVM v, SQChar* fmt, ...)
{
	eDLL_T remoteContext;
	// We use the sqvm pointer as index for SDK usage as the function prototype has to match assembly.
	// The compiler 'pointer truncation' warning couldn't be avoided, but it's safe to ignore it here.
#pragma warning(push)
#pragma warning(disable : 4302 4311)
	switch (static_cast<SQCONTEXT>(reinterpret_cast<int>(v)))
#pragma warning(pop)
	{
	case SQCONTEXT::SERVER:
		remoteContext = eDLL_T::SCRIPT_SERVER;
		break;
	case SQCONTEXT::CLIENT:
		remoteContext = eDLL_T::SCRIPT_CLIENT;
		break;
	case SQCONTEXT::UI:
		remoteContext = eDLL_T::SCRIPT_UI;
		break;
	case SQCONTEXT::NONE:
		remoteContext = eDLL_T::NONE;
		break;
	default:

		SQCONTEXT scriptContext = v->GetContext();
		switch (scriptContext)
		{
		case SQCONTEXT::SERVER:
			remoteContext = eDLL_T::SCRIPT_SERVER;
			break;
		case SQCONTEXT::CLIENT:
			remoteContext = eDLL_T::SCRIPT_CLIENT;
			break;
		case SQCONTEXT::UI:
			remoteContext = eDLL_T::SCRIPT_UI;
			break;
		default:
			remoteContext = eDLL_T::NONE;
			break;
		}
		break;
	}

	// Determine whether this is an info or warning log.
	const bool bLogLevelOverride = (g_bSQAuxError || (g_bSQAuxBadLogic && v == g_pErrorVM));
	LogLevel_t level = LogLevel_t(script_show_output.GetInt());
	LogType_t type = bLogLevelOverride ? LogType_t::SQ_WARNING : LogType_t::SQ_INFO;

	// Always log script related problems to the console.
	if (type == LogType_t::SQ_WARNING &&
		level == LogLevel_t::LEVEL_DISK_ONLY)
	{
		level = LogLevel_t::LEVEL_CONSOLE;
	}

	va_list args;
	va_start(args, fmt);
	CoreMsgV(type, level, remoteContext, "squirrel_re", fmt, args);
	va_end(args);

	return SQ_OK;
}

//---------------------------------------------------------------------------------
// Purpose: prints the warnings of each VM to the console
// Input  : *v -
//          nformatstringidx -  
//---------------------------------------------------------------------------------
SQBool Script_WarningFunc(HSQUIRRELVM v, SQInteger nformatstringidx)
{
	SQInteger strLen = 0;
	SQChar* str = nullptr;

	const SQRESULT result = v_sqstd_format(v, nformatstringidx, SQTrue, &strLen, &str);

	const SQCONTEXT scriptContext = v->GetContext();
	eDLL_T remoteContext;

	switch (scriptContext)
	{
	case SQCONTEXT::SERVER:
		remoteContext = eDLL_T::SCRIPT_SERVER;
		break;
	case SQCONTEXT::CLIENT:
		remoteContext = eDLL_T::SCRIPT_CLIENT;
		break;
	case SQCONTEXT::UI:
		remoteContext = eDLL_T::SCRIPT_UI;
		break;
	default:
		remoteContext = eDLL_T::NONE;
		break;
	}

	CoreMsg(LogType_t::SQ_WARNING, static_cast<LogLevel_t>(script_show_warning.GetInt()),
		remoteContext, NO_ERROR, "squirrel_re(warning)", "%s", str);

	return SQ_SUCCEEDED(result);
}

//---------------------------------------------------------------------------------
void VSquirrel::Detour(const bool bAttach) const
{
	DetourSetup(&CSquirrelVM__Init, &CSquirrelVM::Init, bAttach);
	DetourSetup(&CSquirrelVM__DestroySignalEntryListHead, &CSquirrelVM::DestroySignalEntryListHead, bAttach);
	DetourSetup(&CSquirrelVM__ExecuteFunction, &Script_ExecuteFunction, bAttach);

	DetourSetup(&v_Script_PrintFunc, &Script_PrintFunc, bAttach);
	DetourSetup(&v_Script_WarningFunc, &Script_WarningFunc, bAttach);
}
