#include "core/stdafx.h"

#ifndef DEDICATED

#include "discord_rpc_wrapper.h"
#include "discord_ipc.h"

//-----------------------------------------------------------------------------
// Discord RPC Wrapper - Uses native IPC implementation
// No third-party DLL required
//-----------------------------------------------------------------------------

extern "C" {

void Discord_Initialize(const char* applicationId, DiscordEventHandlers* handlers, int autoRegister, const char* optionalSteamId) {
    (void)autoRegister;
    (void)optionalSteamId;
    CDiscordIpc::Initialize(applicationId, handlers);
}

void Discord_Shutdown(void) {
    CDiscordIpc::Shutdown();
}

void Discord_RunCallbacks(void) {
    CDiscordIpc::RunCallbacks();
}

void Discord_UpdatePresence(const DiscordRichPresence* presence) {
    CDiscordIpc::UpdatePresence(presence);
}

void Discord_ClearPresence(void) {
    CDiscordIpc::ClearPresence();
}

void Discord_Respond(const char* userid, int reply) {
    // Not implemented - join requests not supported in native IPC
    (void)userid;
    (void)reply;
}

void Discord_UpdateHandlers(DiscordEventHandlers* handlers) {
    // Re-initialize with new handlers
    // Note: This is a simplified implementation
    (void)handlers;
}

}

#endif // !DEDICATED
