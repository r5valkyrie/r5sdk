#ifndef SCRIPTNETDATA_CLIENT_H
#define SCRIPTNETDATA_CLIENT_H

//------------------------------------------------------------------------------
// ScriptNetData - Networked Variable Change Callbacks for CLIENT/UI
// Engine only registers these for CLIENT VM; SDK provides them for UI VM too
//------------------------------------------------------------------------------

#include "vscript/vscript.h"

// SNDC - Script Net Data Category
enum ScriptNetDataCategory_e
{
	SNDC_GLOBAL = 0,
	SNDC_GLOBAL_NON_REWIND = 1,  // S10+ addition
	SNDC_PLAYER_GLOBAL = 2,
	SNDC_PLAYER_EXCLUSIVE = 3,
	// 4 = unused
	SNDC_TITAN_SOUL = 5,
	SNDC_DEATH_BOX = 6,

	SNDC_COUNT = 7
};

// SNVT - Script Net Variable Type
enum ScriptNetVarType_e
{
	SNVT_BOOL = 0,
	SNVT_INT = 1,
	SNVT_UNSIGNED_INT = 2,
	SNVT_BIG_INT = 3,
	SNVT_FLOAT_RANGE = 4,
	SNVT_FLOAT_RANGE_OVER_TIME = 5,  // Cannot have callbacks
	SNVT_TIME = 6,
	SNVT_ENTITY = 7,

	SNVT_COUNT = 8
};

//------------------------------------------------------------------------------
// Script Function Registration
//------------------------------------------------------------------------------
void ScriptNetData_RegisterClientFunctions(CSquirrelVM* vm);
void ScriptNetData_RegisterUIFunctions(CSquirrelVM* vm);

//------------------------------------------------------------------------------
// Cleanup - called when VM is destroyed
//------------------------------------------------------------------------------
void ScriptNetData_OnVMDestroyed(CSquirrelVM* vm);

#endif // SCRIPTNETDATA_CLIENT_H
