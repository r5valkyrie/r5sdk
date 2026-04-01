#ifndef SCRIPTREMOTEFUNCTIONS_SERVER_H
#define SCRIPTREMOTEFUNCTIONS_SERVER_H

#include "scriptremotefunctions_shared.h"

class CClient;
class NET_ScriptMessage;

bool ScriptRemoteServer_ProcessMessage(CClient* pClient, NET_ScriptMessage* pMsg);

#endif // SCRIPTREMOTEFUNCTIONS_SERVER_H
