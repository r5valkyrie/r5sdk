#include "core/stdafx.h"
#include "core/r5dev.h"
#include "core/init.h"
#include "core/logdef.h"
#include "core/logger.h"
#include "tier0/cpu.h"
#include "tier0/basetypes.h"
#include "tier0/crashhandler.h"
#include "tier0/commandline.h"
#include "tier2/crashreporter.h"
#include "rtech/pak/pakstate.h"
#ifndef DEDICATED
#include "engine/client/steam_integration.h"
#include "engine/client/clientstate.h"
#include "ebisusdk/EbisuSDK.h"
#include "common/global.h"
#endif
/*****************************************************************************/
#ifndef DEDICATED
#include "windows/id3dx.h"
#include "windows/input.h"
#endif // !DEDICATED
#include "windows/console.h"
#include "windows/system.h"
#include "mathlib/mathlib.h"
#include "launcher/launcher.h"
#include "protobuf/stubs/common.h"
#ifndef DEDICATED
#include "gameui/imgui_system.h"
#endif // !DEDICATED
#include <engine/cmd.h>

#ifndef DEDICATED
#define SDK_DEFAULT_CFG "cfg/system/startup_default.cfg"
#else
#define SDK_DEFAULT_CFG "cfg/system/startup_dedi_default.cfg"
#endif

bool g_bSdkInitialized = false;

bool g_bSdkInitCallInitiated = false;
bool g_bSdkShutdownCallInitiated = false;

bool g_bSdkShutdownInitiatedFromConsoleHandler = false;

static bool s_bConsoleInitialized = false;
static HMODULE s_hModuleHandle = NULL;

//#############################################################################
// UTILITY
//#############################################################################

void Crash_Callback(const CCrashHandler* handler)
{
    CrashReporter_SubmitToCollector(handler);
    SpdLog_Shutdown(); // Shutdown SpdLog to flush all buffers.
}

void Show_Emblem()
{
    // Logged as 'SYSTEM_ERROR' for its red color.
    for (size_t i = 0; i < SDK_ARRAYSIZE(R5R_EMBLEM); i++)
    {
        Msg(eDLL_T::SYSTEM_ERROR, "%s\n", R5R_EMBLEM[i]);
    }

    // Log the SDK's 'build_id' under the emblem.
    Msg(eDLL_T::SYSTEM_ERROR,
        "+---------------------------------------[%s%010u%s]--+\n",
        g_svYellowF.c_str(), g_SDKDll.GetNTHeaders()->FileHeader.TimeDateStamp, g_svRedF.c_str());
    Msg(eDLL_T::SYSTEM_ERROR, "\n");
}

//#############################################################################
// INITIALIZATION
//#############################################################################

void Tier0_Init()
{
#if !defined (DEDICATED)
    g_RadVideoToolsDll.InitFromName("bink2w64.dll");
    g_RadAudioDecoderDll.InitFromName("binkawin64.dll");
    g_RadAudioSystemDll.InitFromName("mileswin64.dll");
#endif // !DEDICATED
    g_CoreMsgVCallback = &EngineLoggerSink; // Setup logger callback sink.

    g_pCmdLine->CreateCmdLine(GetCommandLineA());
    g_CrashHandler.SetCrashCallback(&Crash_Callback);

    // This prevents the game from recreating it,
    // see 'CCommandLine::StaticCreateCmdLine' for
    // more information.
    g_bCommandLineCreated = true;
}

void SDK_Init()
{
    assert(!g_bSdkInitialized);

    CheckSystemCPU(); // Check CPU as early as possible; error out if CPU isn't supported.

    if (g_bSdkInitCallInitiated)
    {
        spdlog::error("Recursive initialization!\n");
        return;
    }

    // Set after checking cpu and initializing MathLib since we check CPU
    // features there. Else we crash on the recursive initialization error as
    // SpdLog uses SSE features.
    g_bSdkInitCallInitiated = true;

    MathLib_Init(); // Initialize Mathlib.

    PEB64* pEnv = CModule::GetProcessEnvironmentBlock();

    g_GameDll.InitFromBase(pEnv->ImageBaseAddress);
    g_SDKDll.InitFromBase((QWORD)s_hModuleHandle);

    Tier0_Init();

    if (!CommandLine()->CheckParm("-launcher"))
    {
        CommandLine()->AppendParametersFromFile(SDK_DEFAULT_CFG);
    }

    const bool bAnsiColor = CommandLine()->CheckParm("-ansicolor") ? true : false;

#ifndef DEDICATED
    if (CommandLine()->CheckParm("-wconsole"))
#else
    if (!CommandLine()->CheckParm("-noconsole"))
#endif  // !DEDICATED
    {
        s_bConsoleInitialized = Console_Init(bAnsiColor);
    }

#ifndef DEDICATED
    ImguiSystem()->SetEnabled(!CommandLine()->CheckParm("-noimgui"));
#endif  // !DEDICATED
    SpdLog_Init(bAnsiColor);
    Show_Emblem();

    GOOGLE_PROTOBUF_VERIFY_VERSION;
    curl_global_init(CURL_GLOBAL_ALL);
    lzham_enable_fail_exceptions(true);

    Winsock_Startup(); // Initialize Winsock.
    DirtySDK_Startup();

    Systems_Init();

    WinSys_Init();
#ifndef DEDICATED
    Input_Init();
#endif // !DEDICATED

#ifndef DEDICATED
    Pak_SetReadPath("paks\\Win64\\");
    Pak_SetWritePath("paks\\Win64_temp\\");
#else
    Pak_SetReadPath("paks\\Win64_server\\");
    Pak_SetWritePath("paks\\Win64_server_temp\\");
#endif

#ifndef DEDICATED
    // Force enable Steam mode for Steam-only authentication (if enabled)
    if (ShouldForceSteamOnly() && !CommandLine()->CheckParm("-noorigin"))
    {
        CommandLine()->AppendParm("-noorigin", "");
        //no need to log this
        //Msg(eDLL_T::ENGINE, "Forced Steam mode for Steam-only authentication\n");
    }
    
    // Check if we're in offline mode first
    if (Steam_IsOfflineMode())
    {
#ifndef USE_STEAMWORKS
        Msg(eDLL_T::STEAM, "Steam offline mode FORCED - compiled without Steamworks support\n");
#else
        Msg(eDLL_T::STEAM, "Steam offline mode detected\n");
#endif
        
        // Get offline Steam data (Steam functions handle offline mode internally)
        uint64_t steamUserID = Steam_GetUserID(); // Returns offline ID in offline mode
        std::string steamUsername;
        Steam_GetUsername(steamUsername); // Returns offline username in offline mode
        
        Msg(eDLL_T::STEAM, "Using offline Steam data: %s (ID: %llu)\n", steamUsername.c_str(), steamUserID);
        
        // Set platform_user_id and g_SteamUserID to offline Steam ID
        if (steamUserID != 0)
        {
            if (platform_user_id)
            {
                platform_user_id->SetValue(Format("%llu", steamUserID).c_str());
            }
            
            if (g_SteamUserID)
            {
                *g_SteamUserID = steamUserID;
            }
        }
        
        // Set offline username as default persona name
        SetSteamPersonaName();
    }
    else
    {
#ifndef USE_STEAMWORKS
        // Steamworks not available - force offline mode
        Msg(eDLL_T::STEAM, "Steamworks not compiled in - falling back to offline mode\n");
        
        // Use offline Steam data
        uint64_t steamUserID = Steam_GetUserID(); // Returns offline ID when no Steamworks
        std::string steamUsername;
        Steam_GetUsername(steamUsername); // Returns offline username when no Steamworks
        
        Msg(eDLL_T::STEAM, "Using offline Steam data: %s (ID: %llu)\n", steamUsername.c_str(), steamUserID);
        
        // Set platform_user_id and g_SteamUserID to offline Steam ID
        if (platform_user_id && steamUserID != 0)
        {
            platform_user_id->SetValue(Format("%llu", steamUserID).c_str());
        }
        
        if (g_SteamUserID && steamUserID != 0)
        {
            *g_SteamUserID = steamUserID;
        }
        
        // Set offline username as default persona name
        SetSteamPersonaName();
#else
        // Initialize Steam for online authentication
        if (Steam_EnsureInitialized())
        {
            Msg(eDLL_T::STEAM, "Steam Initialized\n");
            // Log Steam user information for debugging first
            uint64_t steamUserID = Steam_GetUserID();
            std::string steamUsername;
            if (Steam_GetUsername(steamUsername) && !steamUsername.empty())
            {
                Msg(eDLL_T::STEAM, "Steam User: %s (ID: %llu)\n", steamUsername.c_str(), steamUserID);
            }

            // Set g_SteamUserID to Steam user ID
            if (steamUserID != 0 && g_SteamUserID)
            {
                *g_SteamUserID = steamUserID;
            }

            // Set Steam username as default persona name (may need retry later)
            SetSteamPersonaName();
        }
        else
        {
            // Steam initialization failed - use offline Steam data
            uint64_t steamUserID = Steam_GetUserID(); // Returns offline ID from config
            std::string steamUsername;
            Steam_GetUsername(steamUsername); // Returns offline username from config

            Msg(eDLL_T::STEAM, "Using offline Steam data: %s (ID: %llu)\n", steamUsername.c_str(), steamUserID);

            if (steamUserID != 0 && g_SteamUserID)
            {
                *g_SteamUserID = steamUserID;
            }

            SetSteamPersonaName();
        }
#endif
    }
#endif


    g_bSdkInitialized = true;
}

//#############################################################################
// SHUTDOWN
//#############################################################################

void SDK_Shutdown()
{
    assert(g_bSdkInitialized);

    // Also check CPU in shutdown, since this function is exported, if they
    // call this with an unsupported CPU we should let them know rather than
    // crashing the process.
    CheckSystemCPU();

    if (g_bSdkShutdownCallInitiated)
    {
        spdlog::error("Recursive shutdown!\n");
        return;
    }

    g_bSdkShutdownCallInitiated = true;

    if (!g_bSdkInitialized)
    {
        spdlog::error("Not initialized!\n");
        return;
    }

    Msg(eDLL_T::NONE, "GameSDK shutdown initiated\n");

#ifndef DEDICATED
    // Shutdown Steam FIRST to avoid conflicts with other systems
    Msg(eDLL_T::ENGINE, "[STEAM_SHUTDOWN] Shutting down Steam API...\n");
    Steam_Shutdown();
    
    Input_Shutdown();
#endif // !DEDICATED

    WinSys_Shutdown();
    Systems_Shutdown();

    DirtySDK_Shutdown();
    Winsock_Shutdown();

    curl_global_cleanup();
    google::protobuf::ShutdownProtobufLibrary();

    SpdLog_Shutdown();

    // If the shutdown was initiated from the console window itself, don't
    // shutdown the console as it would otherwise deadlock in FreeConsole!
    if (s_bConsoleInitialized && !g_bSdkShutdownInitiatedFromConsoleHandler)
        Console_Shutdown();

    g_bSdkInitialized = false;
}

//#############################################################################
// ENTRYPOINT
//#############################################################################

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
    NOTE_UNUSED(lpReserved);

    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
        {
            s_hModuleHandle = hModule;
            break;
        }
        case DLL_PROCESS_DETACH:
        {
            s_hModuleHandle = NULL;
            break;
        }
    }

    return TRUE;
}
