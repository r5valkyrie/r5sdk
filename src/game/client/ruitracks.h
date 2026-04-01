//=============================================================================//
// Purpose: RUI_TRACK extended compatibility layer
//=============================================================================//

#ifndef RUITRACKS_H
#define RUITRACKS_H

#include "thirdparty/detours/include/idetour.h"

class CSquirrelVM;

void RuiTracks_Init();
void RuiTracks_RegisterMissingEnums(CSquirrelVM* const s);

class VRuiTracks : public IDetour
{
    virtual void GetAdr(void) const;
    virtual void GetFun(void) const;
    virtual void GetVar(void) const;
    virtual void GetCon(void) const;
    virtual void Detour(const bool bAttach) const;
};

#endif // RUITRACKS_H
