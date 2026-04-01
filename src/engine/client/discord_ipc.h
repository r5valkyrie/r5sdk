#pragma once

#ifndef DEDICATED

// Native Discord IPC implementation
// Communicates directly with Discord via named pipe IPC
// No third-party DLL required

#include "core/stdafx.h"
#include <cstdint>

// Forward declarations
struct DiscordRPCUser;
struct DiscordEventHandlers;
struct DiscordRichPresence;

//-----------------------------------------------------------------------------
// Discord IPC Message Types
//-----------------------------------------------------------------------------
enum class DiscordIpcOpcode : uint32_t
{
    HANDSHAKE = 0,
    FRAME = 1,
    CLOSE = 2,
    PING = 3,
    PONG = 4
};

//-----------------------------------------------------------------------------
// Native Discord IPC Connection
//-----------------------------------------------------------------------------
class CDiscordIpc
{
public:
    static bool Initialize(const char* applicationId, DiscordEventHandlers* handlers);
    static void Shutdown();
    static void RunCallbacks();
    static bool UpdatePresence(const DiscordRichPresence* presence);
    static void ClearPresence();
    static bool IsConnected();

private:
    static bool Connect();
    static void Disconnect();
    static bool SendHandshake();
    static bool SendFrame(const char* jsonPayload);
    static bool ReadMessage();
    static void ProcessMessage(const char* json, size_t length);
    static void OnReady(const char* userId, const char* username, const char* discriminator, const char* avatar);
    static void OnDisconnected(int errorCode, const char* message);
    static void OnError(int errorCode, const char* message);

    // Build JSON payloads
    static size_t BuildSetActivityPayload(char* buffer, size_t bufferSize, const DiscordRichPresence* presence);
    static size_t BuildClearActivityPayload(char* buffer, size_t bufferSize);

    static HANDLE s_hPipe;
    static bool s_bConnected;
    static bool s_bInitialized;
    static char s_szApplicationId[32];
    static uint32_t s_nNonce;
    static DiscordEventHandlers s_Handlers;

    // Read buffer for incoming messages
    static char s_szReadBuffer[16384];
    static size_t s_nReadBufferPos;
};

#endif // !DEDICATED
