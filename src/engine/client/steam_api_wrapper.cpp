// Pure Steam API wrapper - no r5sdk headers to avoid type conflicts
#ifdef USE_STEAMWORKS
#include "steam/steam_api.h"
#endif

#include <string>

extern "C" {
    // C-style interface to avoid C++ name mangling and type conflicts
    
#ifdef USE_STEAMWORKS
    // Store the current auth ticket handle for invalidation
    static HAuthTicket g_CurrentAuthTicket = k_HAuthTicketInvalid;
#endif
    
    int Steam_InitAPI() {
#ifdef USE_STEAMWORKS
        // Check if we need to restart through Steam
        if (SteamAPI_RestartAppIfNecessary(480)) // Spacewar App ID for testing
        {
            // App was not launched through Steam, restart through Steam
            return 0;
        }
        
        bool result = SteamAPI_Init();
        return result ? 1 : 0;
#else
        return 0;
#endif
    }
    
    void Steam_ShutdownAPI() {
#ifdef USE_STEAMWORKS
        SteamAPI_Shutdown();
#endif
    }
    
    void Steam_RunCallbacksAPI() {
#ifdef USE_STEAMWORKS
        SteamAPI_RunCallbacks();
#endif
    }
    
    int Steam_GetUsernameC(char* outBuffer, int bufferSize) {
#ifdef USE_STEAMWORKS
        if (!outBuffer || bufferSize < 1) {
            return 0;
        }
        
        if (!SteamUser()) {
            return 0;
        }
        
        const char* username = SteamFriends()->GetPersonaName();
        if (!username) {
            return 0;
        }
        
        int len = (int)strlen(username);
        if (bufferSize < len + 1) {
            return 0; // Buffer too small
        }
        
        strcpy_s(outBuffer, bufferSize, username);
        return len;
#else
        return 0;
#endif
    }
    
    unsigned long long Steam_GetUserIDC() {
#ifdef USE_STEAMWORKS
        if (!SteamUser()) {
            return 0;
        }
        
        CSteamID steamID = SteamUser()->GetSteamID();
        return steamID.ConvertToUint64();
#else
        return 0;
#endif
    }

    int Steam_GetAuthTicketHex(char* outBuffer, int bufferSize) {
#ifdef USE_STEAMWORKS
        if (!outBuffer || bufferSize < 1) {
            return 0;
        }
        
        if (!SteamUser()) {
            return 0;
        }
        
        // Invalidate any existing ticket first to ensure fresh ticket generation
        if (g_CurrentAuthTicket != k_HAuthTicketInvalid) {
            SteamUser()->CancelAuthTicket(g_CurrentAuthTicket);
            g_CurrentAuthTicket = k_HAuthTicketInvalid;
        }
        
        uint8 ticketBuf[4096];
        uint32 ticketLen = 0;
        HAuthTicket h = SteamUser()->GetAuthSessionTicket(ticketBuf, (int)sizeof(ticketBuf), &ticketLen, nullptr);
        
        if (h == k_HAuthTicketInvalid || ticketLen == 0) {
            return 0;
        }
        
        // Store the handle for later invalidation
        g_CurrentAuthTicket = h;
        
        // Need space for hex encoding (2 chars per byte) + null terminator
        if (bufferSize < (int)(ticketLen * 2 + 1)) {
            return 0;
        }
        
        static const char* hex = "0123456789ABCDEF";
        for (uint32 i = 0; i < ticketLen; ++i) {
            outBuffer[i*2+0] = hex[(ticketBuf[i] >> 4) & 0xF];
            outBuffer[i*2+1] = hex[ticketBuf[i] & 0xF];
        }
        outBuffer[ticketLen * 2] = '\0';
        
        return (int)(ticketLen * 2);
#else
        return 0;
#endif
    }
    
    void Steam_CancelAuthTicket() {
#ifdef USE_STEAMWORKS
        if (g_CurrentAuthTicket != k_HAuthTicketInvalid && SteamUser()) {
            SteamUser()->CancelAuthTicket(g_CurrentAuthTicket);
            g_CurrentAuthTicket = k_HAuthTicketInvalid;
        }
#endif
    }
}

extern "C" int Steam_IsOverlayEnabled()
{
#ifndef STEAMWORKS_CLIENT_INTERFACES
    return 0;
#else
    if (!SteamUtils()) {
        return 0;
    }
    return SteamUtils()->IsOverlayEnabled() ? 1 : 0;
#endif
}

extern "C" void Steam_SetOverlayNotificationPosition(int position)
{
    (void)position; // May be unused if STEAMWORKS_CLIENT_INTERFACES is not defined
#ifdef STEAMWORKS_CLIENT_INTERFACES
    if (SteamUtils()) {
        // position: 0=top-left, 1=top-right, 2=bottom-left, 3=bottom-right
        ENotificationPosition steamPos = k_EPositionTopLeft;
        switch (position) {
            case 1: steamPos = k_EPositionTopRight; break;
            case 2: steamPos = k_EPositionBottomLeft; break;
            case 3: steamPos = k_EPositionBottomRight; break;
            default: steamPos = k_EPositionTopLeft; break;
        }
        SteamUtils()->SetOverlayNotificationPosition(steamPos);
    }
#endif
}

