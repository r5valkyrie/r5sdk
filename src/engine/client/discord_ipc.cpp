#include "core/stdafx.h"

#ifndef DEDICATED

#include "discord_ipc.h"
#include "discord_rpc_wrapper.h"
#include "tier0/commandline.h"
#include "tier0/platform.h"

//-----------------------------------------------------------------------------
// Discord IPC Protocol Constants
//-----------------------------------------------------------------------------
#define DISCORD_IPC_VERSION 1
#define DISCORD_RPC_PIPE_NAME "\\\\.\\pipe\\discord-ipc-0"
#define DISCORD_MAX_MESSAGE_SIZE 65536

#pragma pack(push, 1)
struct DiscordIpcHeader
{
    uint32_t opcode;
    uint32_t length;
};
#pragma pack(pop)

//-----------------------------------------------------------------------------
// Static member definitions
//-----------------------------------------------------------------------------
HANDLE CDiscordIpc::s_hPipe = INVALID_HANDLE_VALUE;
bool CDiscordIpc::s_bConnected = false;
bool CDiscordIpc::s_bInitialized = false;
char CDiscordIpc::s_szApplicationId[32] = { 0 };
uint32_t CDiscordIpc::s_nNonce = 0;
DiscordEventHandlers CDiscordIpc::s_Handlers = {};
char CDiscordIpc::s_szReadBuffer[16384] = { 0 };
size_t CDiscordIpc::s_nReadBufferPos = 0;

//-----------------------------------------------------------------------------
// Purpose: Initialize Discord IPC connection
//-----------------------------------------------------------------------------
bool CDiscordIpc::Initialize(const char* applicationId, DiscordEventHandlers* handlers)
{
    if (s_bInitialized)
        return true;

    // Validate Application ID
    if (!applicationId || strlen(applicationId) == 0 || strlen(applicationId) >= sizeof(s_szApplicationId))
    {
        DevMsg(eDLL_T::CLIENT, "Discord IPC: Invalid application ID\n");
        return false;
    }

    // Validate that appId contains only digits
    for (const char* p = applicationId; *p; ++p)
    {
        if (*p < '0' || *p > '9')
        {
            DevMsg(eDLL_T::CLIENT, "Discord IPC: Invalid application ID format\n");
            return false;
        }
    }

    V_strncpy(s_szApplicationId, applicationId, sizeof(s_szApplicationId));

    if (handlers)
    {
        s_Handlers = *handlers;
    }

    s_nNonce = 0;
    s_nReadBufferPos = 0;
    s_bInitialized = true;

    DevMsg(eDLL_T::CLIENT, "Discord IPC: Initialized with App ID: %s\n", s_szApplicationId);

    // Attempt initial connection
    Connect();

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: Shutdown Discord IPC
//-----------------------------------------------------------------------------
void CDiscordIpc::Shutdown()
{
    if (!s_bInitialized)
        return;

    Disconnect();

    s_bInitialized = false;
    memset(s_szApplicationId, 0, sizeof(s_szApplicationId));
    memset(&s_Handlers, 0, sizeof(s_Handlers));

    DevMsg(eDLL_T::CLIENT, "Discord IPC: Shutdown\n");
}

//-----------------------------------------------------------------------------
// Purpose: Process callbacks and reconnection
//-----------------------------------------------------------------------------
void CDiscordIpc::RunCallbacks()
{
    if (!s_bInitialized)
        return;

    // Try to connect if not connected
    if (!s_bConnected)
    {
        static double s_flNextConnectAttempt = 0.0;
        double flCurTime = Plat_FloatTime();

        if (flCurTime >= s_flNextConnectAttempt)
        {
            if (Connect())
            {
                DevMsg(eDLL_T::CLIENT, "Discord IPC: Connected\n");
            }
            else
            {
                // Retry in 5 seconds
                s_flNextConnectAttempt = flCurTime + 5.0;
            }
        }
        return;
    }

    // Read any incoming messages
    ReadMessage();
}

//-----------------------------------------------------------------------------
// Purpose: Connect to Discord IPC pipe
//-----------------------------------------------------------------------------
bool CDiscordIpc::Connect()
{
    if (s_bConnected)
        return true;

    // Try all possible pipe numbers (0-9)
    for (int i = 0; i < 10; i++)
    {
        char pipeName[64];
        V_snprintf(pipeName, sizeof(pipeName), "\\\\.\\pipe\\discord-ipc-%d", i);

        s_hPipe = CreateFileA(
            pipeName,
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );

        if (s_hPipe != INVALID_HANDLE_VALUE)
        {
            DevMsg(eDLL_T::CLIENT, "Discord IPC: Connected to pipe %s\n", pipeName);

            // Set pipe to message mode
            DWORD mode = PIPE_READMODE_BYTE;
            SetNamedPipeHandleState(s_hPipe, &mode, nullptr, nullptr);

            // Send handshake
            if (SendHandshake())
            {
                s_bConnected = true;
                return true;
            }
            else
            {
                CloseHandle(s_hPipe);
                s_hPipe = INVALID_HANDLE_VALUE;
            }
        }
    }

    return false;
}

//-----------------------------------------------------------------------------
// Purpose: Disconnect from Discord IPC
//-----------------------------------------------------------------------------
void CDiscordIpc::Disconnect()
{
    if (s_hPipe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(s_hPipe);
        s_hPipe = INVALID_HANDLE_VALUE;
    }

    s_bConnected = false;
    s_nReadBufferPos = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Send handshake to Discord
//-----------------------------------------------------------------------------
bool CDiscordIpc::SendHandshake()
{
    char payload[256];
    int len = V_snprintf(payload, sizeof(payload),
        "{\"v\":%d,\"client_id\":\"%s\"}",
        DISCORD_IPC_VERSION, s_szApplicationId);

    DiscordIpcHeader header;
    header.opcode = static_cast<uint32_t>(DiscordIpcOpcode::HANDSHAKE);
    header.length = static_cast<uint32_t>(len);

    DWORD bytesWritten;
    
    // Write header
    if (!WriteFile(s_hPipe, &header, sizeof(header), &bytesWritten, nullptr) || bytesWritten != sizeof(header))
    {
        DevMsg(eDLL_T::CLIENT, "Discord IPC: Failed to write handshake header\n");
        return false;
    }

    // Write payload
    if (!WriteFile(s_hPipe, payload, len, &bytesWritten, nullptr) || bytesWritten != static_cast<DWORD>(len))
    {
        DevMsg(eDLL_T::CLIENT, "Discord IPC: Failed to write handshake payload\n");
        return false;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: Send a frame to Discord
//-----------------------------------------------------------------------------
bool CDiscordIpc::SendFrame(const char* jsonPayload)
{
    if (!s_bConnected || s_hPipe == INVALID_HANDLE_VALUE)
        return false;

    size_t len = strlen(jsonPayload);
    if (len > DISCORD_MAX_MESSAGE_SIZE)
        return false;

    DiscordIpcHeader header;
    header.opcode = static_cast<uint32_t>(DiscordIpcOpcode::FRAME);
    header.length = static_cast<uint32_t>(len);

    DWORD bytesWritten;

    // Write header
    if (!WriteFile(s_hPipe, &header, sizeof(header), &bytesWritten, nullptr) || bytesWritten != sizeof(header))
    {
        Disconnect();
        return false;
    }

    // Write payload
    if (!WriteFile(s_hPipe, jsonPayload, static_cast<DWORD>(len), &bytesWritten, nullptr) || bytesWritten != static_cast<DWORD>(len))
    {
        Disconnect();
        return false;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: Read messages from Discord
//-----------------------------------------------------------------------------
bool CDiscordIpc::ReadMessage()
{
    if (!s_bConnected || s_hPipe == INVALID_HANDLE_VALUE)
        return false;

    // Check if data is available (non-blocking)
    DWORD bytesAvailable = 0;
    if (!PeekNamedPipe(s_hPipe, nullptr, 0, nullptr, &bytesAvailable, nullptr))
    {
        Disconnect();
        return false;
    }

    if (bytesAvailable == 0)
        return true;

    // Read header
    DiscordIpcHeader header;
    DWORD bytesRead;

    if (!ReadFile(s_hPipe, &header, sizeof(header), &bytesRead, nullptr) || bytesRead != sizeof(header))
    {
        Disconnect();
        return false;
    }

    if (header.length > sizeof(s_szReadBuffer) - 1)
    {
        DevMsg(eDLL_T::CLIENT, "Discord IPC: Message too large (%u bytes)\n", header.length);
        Disconnect();
        return false;
    }

    // Read payload
    if (!ReadFile(s_hPipe, s_szReadBuffer, header.length, &bytesRead, nullptr) || bytesRead != header.length)
    {
        Disconnect();
        return false;
    }

    s_szReadBuffer[header.length] = '\0';

    // Process the message
    switch (static_cast<DiscordIpcOpcode>(header.opcode))
    {
    case DiscordIpcOpcode::FRAME:
        ProcessMessage(s_szReadBuffer, header.length);
        break;

    case DiscordIpcOpcode::CLOSE:
        DevMsg(eDLL_T::CLIENT, "Discord IPC: Received close\n");
        Disconnect();
        break;

    case DiscordIpcOpcode::PING:
        // Respond with pong
        {
            DiscordIpcHeader pongHeader;
            pongHeader.opcode = static_cast<uint32_t>(DiscordIpcOpcode::PONG);
            pongHeader.length = header.length;

            DWORD bytesWritten;
            WriteFile(s_hPipe, &pongHeader, sizeof(pongHeader), &bytesWritten, nullptr);
            WriteFile(s_hPipe, s_szReadBuffer, header.length, &bytesWritten, nullptr);
        }
        break;

    default:
        break;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: Process incoming JSON message
//-----------------------------------------------------------------------------
void CDiscordIpc::ProcessMessage(const char* json, size_t length)
{
    // Simple JSON parsing for READY event
    // Look for "evt":"READY" and "cmd":"DISPATCH"
    if (strstr(json, "\"evt\":\"READY\"") || strstr(json, "\"cmd\":\"DISPATCH\""))
    {
        // Extract user information (simplified parsing)
        const char* userIdStart = strstr(json, "\"id\":\"");
        const char* usernameStart = strstr(json, "\"username\":\"");
        const char* discriminatorStart = strstr(json, "\"discriminator\":\"");

        char userId[32] = { 0 };
        char username[64] = { 0 };
        char discriminator[8] = { 0 };

        if (userIdStart)
        {
            userIdStart += 6;
            const char* userIdEnd = strchr(userIdStart, '\"');
            if (userIdEnd && (userIdEnd - userIdStart) < sizeof(userId))
            {
                V_strncpy(userId, userIdStart, min(sizeof(userId), static_cast<size_t>(userIdEnd - userIdStart + 1)));
            }
        }

        if (usernameStart)
        {
            usernameStart += 12;
            const char* usernameEnd = strchr(usernameStart, '\"');
            if (usernameEnd && (usernameEnd - usernameStart) < sizeof(username))
            {
                V_strncpy(username, usernameStart, min(sizeof(username), static_cast<size_t>(usernameEnd - usernameStart + 1)));
            }
        }

        if (discriminatorStart)
        {
            discriminatorStart += 17;
            const char* discriminatorEnd = strchr(discriminatorStart, '\"');
            if (discriminatorEnd && (discriminatorEnd - discriminatorStart) < sizeof(discriminator))
            {
                V_strncpy(discriminator, discriminatorStart, min(sizeof(discriminator), static_cast<size_t>(discriminatorEnd - discriminatorStart + 1)));
            }
        }

        OnReady(userId, username, discriminator, "");
    }
    else if (strstr(json, "\"evt\":\"ERROR\""))
    {
        OnError(-1, "Discord RPC Error");
    }
}

//-----------------------------------------------------------------------------
// Purpose: Called when Discord is ready
//-----------------------------------------------------------------------------
void CDiscordIpc::OnReady(const char* userId, const char* username, const char* discriminator, const char* avatar)
{
    DevMsg(eDLL_T::CLIENT, "Discord IPC: Ready (user: %s#%s)\n", username, discriminator);

    if (s_Handlers.ready)
    {
        DiscordRPCUser user;
        user.userId = userId;
        user.username = username;
        user.discriminator = discriminator;
        user.avatar = avatar;
        s_Handlers.ready(&user);
    }
}

//-----------------------------------------------------------------------------
// Purpose: Called when Discord disconnects
//-----------------------------------------------------------------------------
void CDiscordIpc::OnDisconnected(int errorCode, const char* message)
{
    DevMsg(eDLL_T::CLIENT, "Discord IPC: Disconnected (%d: %s)\n", errorCode, message ? message : "");

    if (s_Handlers.disconnected)
    {
        s_Handlers.disconnected(errorCode, message);
    }
}

//-----------------------------------------------------------------------------
// Purpose: Called on Discord error
//-----------------------------------------------------------------------------
void CDiscordIpc::OnError(int errorCode, const char* message)
{
    DevMsg(eDLL_T::CLIENT, "Discord IPC: Error (%d: %s)\n", errorCode, message ? message : "");

    if (s_Handlers.errored)
    {
        s_Handlers.errored(errorCode, message);
    }
}

//-----------------------------------------------------------------------------
// Purpose: Update Discord presence
//-----------------------------------------------------------------------------
bool CDiscordIpc::UpdatePresence(const DiscordRichPresence* presence)
{
    if (!s_bConnected || !presence)
        return false;

    char buffer[8192];
    size_t len = BuildSetActivityPayload(buffer, sizeof(buffer), presence);
    if (len == 0)
        return false;

    return SendFrame(buffer);
}

//-----------------------------------------------------------------------------
// Purpose: Clear Discord presence
//-----------------------------------------------------------------------------
void CDiscordIpc::ClearPresence()
{
    if (!s_bConnected)
        return;

    char buffer[512];
    size_t len = BuildClearActivityPayload(buffer, sizeof(buffer));
    if (len > 0)
    {
        SendFrame(buffer);
    }
}

//-----------------------------------------------------------------------------
// Purpose: Check if connected
//-----------------------------------------------------------------------------
bool CDiscordIpc::IsConnected()
{
    return s_bConnected;
}

//-----------------------------------------------------------------------------
// Helper: Escape JSON string
//-----------------------------------------------------------------------------
static size_t EscapeJsonString(char* dest, size_t destSize, const char* src)
{
    if (!src || !dest || destSize == 0)
        return 0;

    size_t pos = 0;
    while (*src && pos < destSize - 1)
    {
        char c = *src++;
        if (c == '\"' || c == '\\')
        {
            if (pos + 2 >= destSize)
                break;
            dest[pos++] = '\\';
            dest[pos++] = c;
        }
        else if (c == '\n')
        {
            if (pos + 2 >= destSize)
                break;
            dest[pos++] = '\\';
            dest[pos++] = 'n';
        }
        else if (c == '\r')
        {
            if (pos + 2 >= destSize)
                break;
            dest[pos++] = '\\';
            dest[pos++] = 'r';
        }
        else if (c == '\t')
        {
            if (pos + 2 >= destSize)
                break;
            dest[pos++] = '\\';
            dest[pos++] = 't';
        }
        else if (c >= 0x20)
        {
            dest[pos++] = c;
        }
    }
    dest[pos] = '\0';
    return pos;
}

//-----------------------------------------------------------------------------
// Purpose: Build SET_ACTIVITY payload
//-----------------------------------------------------------------------------
size_t CDiscordIpc::BuildSetActivityPayload(char* buffer, size_t bufferSize, const DiscordRichPresence* presence)
{
    char escapedState[256];
    char escapedDetails[256];
    char escapedLargeImageKey[64];
    char escapedLargeImageText[256];
    char escapedSmallImageKey[64];
    char escapedSmallImageText[256];
    char escapedPartyId[256];

    size_t pos = 0;
    uint32_t nonce = ++s_nNonce;

    // Start building JSON
    pos += V_snprintf(buffer + pos, bufferSize - pos,
        "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lu,\"activity\":{",
        GetCurrentProcessId());

    bool needsComma = false;

    // State
    if (presence->state && presence->state[0])
    {
        EscapeJsonString(escapedState, sizeof(escapedState), presence->state);
        pos += V_snprintf(buffer + pos, bufferSize - pos,
            "\"state\":\"%s\"", escapedState);
        needsComma = true;
    }

    // Details
    if (presence->details && presence->details[0])
    {
        EscapeJsonString(escapedDetails, sizeof(escapedDetails), presence->details);
        pos += V_snprintf(buffer + pos, bufferSize - pos,
            "%s\"details\":\"%s\"",
            needsComma ? "," : "", escapedDetails);
        needsComma = true;
    }

    // Timestamps
    if (presence->startTimestamp > 0 || presence->endTimestamp > 0)
    {
        pos += V_snprintf(buffer + pos, bufferSize - pos,
            "%s\"timestamps\":{", needsComma ? "," : "");
        
        bool timestampComma = false;
        if (presence->startTimestamp > 0)
        {
            pos += V_snprintf(buffer + pos, bufferSize - pos,
                "\"start\":%lld", presence->startTimestamp);
            timestampComma = true;
        }
        if (presence->endTimestamp > 0)
        {
            pos += V_snprintf(buffer + pos, bufferSize - pos,
                "%s\"end\":%lld",
                timestampComma ? "," : "", presence->endTimestamp);
        }
        pos += V_snprintf(buffer + pos, bufferSize - pos, "}");
        needsComma = true;
    }

    // Assets
    bool hasAssets = (presence->largeImageKey && presence->largeImageKey[0]) ||
                     (presence->smallImageKey && presence->smallImageKey[0]);
    if (hasAssets)
    {
        pos += V_snprintf(buffer + pos, bufferSize - pos,
            "%s\"assets\":{", needsComma ? "," : "");
        
        bool assetComma = false;
        if (presence->largeImageKey && presence->largeImageKey[0])
        {
            EscapeJsonString(escapedLargeImageKey, sizeof(escapedLargeImageKey), presence->largeImageKey);
            pos += V_snprintf(buffer + pos, bufferSize - pos,
                "\"large_image\":\"%s\"", escapedLargeImageKey);
            assetComma = true;

            if (presence->largeImageText && presence->largeImageText[0])
            {
                EscapeJsonString(escapedLargeImageText, sizeof(escapedLargeImageText), presence->largeImageText);
                pos += V_snprintf(buffer + pos, bufferSize - pos,
                    ",\"large_text\":\"%s\"", escapedLargeImageText);
            }
        }

        if (presence->smallImageKey && presence->smallImageKey[0])
        {
            EscapeJsonString(escapedSmallImageKey, sizeof(escapedSmallImageKey), presence->smallImageKey);
            pos += V_snprintf(buffer + pos, bufferSize - pos,
                "%s\"small_image\":\"%s\"",
                assetComma ? "," : "", escapedSmallImageKey);

            if (presence->smallImageText && presence->smallImageText[0])
            {
                EscapeJsonString(escapedSmallImageText, sizeof(escapedSmallImageText), presence->smallImageText);
                pos += V_snprintf(buffer + pos, bufferSize - pos,
                    ",\"small_text\":\"%s\"", escapedSmallImageText);
            }
        }

        pos += V_snprintf(buffer + pos, bufferSize - pos, "}");
        needsComma = true;
    }

    // Party
    bool hasParty = (presence->partyId && presence->partyId[0]) ||
                    (presence->partySize > 0 && presence->partyMax > 0);
    if (hasParty)
    {
        pos += V_snprintf(buffer + pos, bufferSize - pos,
            "%s\"party\":{", needsComma ? "," : "");
        
        bool partyComma = false;
        if (presence->partyId && presence->partyId[0])
        {
            EscapeJsonString(escapedPartyId, sizeof(escapedPartyId), presence->partyId);
            pos += V_snprintf(buffer + pos, bufferSize - pos,
                "\"id\":\"%s\"", escapedPartyId);
            partyComma = true;
        }

        if (presence->partySize > 0 && presence->partyMax > 0)
        {
            pos += V_snprintf(buffer + pos, bufferSize - pos,
                "%s\"size\":[%d,%d]",
                partyComma ? "," : "", presence->partySize, presence->partyMax);
        }

        pos += V_snprintf(buffer + pos, bufferSize - pos, "}");
        needsComma = true;
    }

    // Secrets
    bool hasSecrets = (presence->matchSecret && presence->matchSecret[0]) ||
                      (presence->joinSecret && presence->joinSecret[0]) ||
                      (presence->spectateSecret && presence->spectateSecret[0]);
    if (hasSecrets)
    {
        pos += V_snprintf(buffer + pos, bufferSize - pos,
            "%s\"secrets\":{", needsComma ? "," : "");
        
        bool secretComma = false;
        if (presence->matchSecret && presence->matchSecret[0])
        {
            pos += V_snprintf(buffer + pos, bufferSize - pos,
                "\"match\":\"%s\"", presence->matchSecret);
            secretComma = true;
        }
        if (presence->joinSecret && presence->joinSecret[0])
        {
            pos += V_snprintf(buffer + pos, bufferSize - pos,
                "%s\"join\":\"%s\"",
                secretComma ? "," : "", presence->joinSecret);
            secretComma = true;
        }
        if (presence->spectateSecret && presence->spectateSecret[0])
        {
            pos += V_snprintf(buffer + pos, bufferSize - pos,
                "%s\"spectate\":\"%s\"",
                secretComma ? "," : "", presence->spectateSecret);
        }

        pos += V_snprintf(buffer + pos, bufferSize - pos, "}");
        needsComma = true;
    }

    // Instance
    pos += V_snprintf(buffer + pos, bufferSize - pos,
        "%s\"instance\":%s",
        needsComma ? "," : "", presence->instance ? "true" : "false");

    // Close activity and args objects
    pos += V_snprintf(buffer + pos, bufferSize - pos,
        "}},\"nonce\":\"%u\"}", nonce);

    return pos;
}

//-----------------------------------------------------------------------------
// Purpose: Build CLEAR_ACTIVITY payload
//-----------------------------------------------------------------------------
size_t CDiscordIpc::BuildClearActivityPayload(char* buffer, size_t bufferSize)
{
    uint32_t nonce = ++s_nNonce;
    return V_snprintf(buffer, bufferSize,
        "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lu},\"nonce\":\"%u\"}",
        GetCurrentProcessId(), nonce);
}

#endif // !DEDICATED
