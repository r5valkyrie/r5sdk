#ifndef RUI_WALLTIME_H
#define RUI_WALLTIME_H

class CSquirrelVM;

void RuiWallTime_Init();
void RuiWallTime_RegisterNatives(CSquirrelVM* s);

class VRuiWallTime : public IDetour
{
    virtual void GetAdr(void) const;
    virtual void GetFun(void) const;
    virtual void GetVar(void) const;
    virtual void GetCon(void) const;
    virtual void Detour(const bool bAttach) const;
};

#endif // RUI_WALLTIME_H
