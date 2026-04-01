//=============================================================================//
//
// Purpose: Implement things from GameInterface.cpp. Mostly the engine interfaces.
//
// $NoKeywords: $
//=============================================================================//

#include "core/stdafx.h"
#include "tier1/cvar.h"
#include "public/server_class.h"
#include "public/eiface.h"
#include "public/const.h"
#include "common/protocol.h"
#include "common/callback.h"
#include "rtech/liveapi/liveapi.h"
#include "engine/server/sv_main.h"
#include "gameinterface.h"
#include "entitylist.h"
#include "baseanimating.h"
#include "engine/server/server.h"
#include "game/shared/usercmd.h"
#include "game/server/util_server.h"
#include "pluginsystem/pluginsystem.h"
#include "game/server/recipientfilter.h"
#include "game/shared/weapon_script_vars.h"
#include "game/shared/weapon_heat.h"
#include "game/shared/globalnonrewind_vars.h"
#include "game/shared/deathfield_system.h"
#include "game/shared/highlight_context.h"
#ifndef DEDICATED
#include "game/client/vscript_remotefunctions.h"
#include "game/client/vscript_colorpalette.h"
#include "game/client/vscript_player.h"
#endif

//-----------------------------------------------------------------------------
// Purpose: retrieves the index of the client that issued the last command
// Output : int
//-----------------------------------------------------------------------------
int UTIL_GetCommandClientIndex(void)
{
	// -1 == unknown,dedicated server console
	// 0  == player 1

	// Convert to 1 based offset
	return (*g_nCommandClientIndex)+1;
}

//-----------------------------------------------------------------------------
// Purpose: retrieves the player of the client that issued the last command
// Output : CPlayer*
//-----------------------------------------------------------------------------
CPlayer* UTIL_GetCommandClient(void)
{
	const int idx = UTIL_GetCommandClientIndex();
	if (idx > 0)
	{
		CPlayer* const player = UTIL_PlayerByIndex(idx);

		if (!player || !player->IsConnected())
			return NULL;

		return player;
	}

	// HLDS console issued command
	return NULL;
}

bool CServerGameDLL::DLLInit(CServerGameDLL* thisptr, CreateInterfaceFn appSystemFactory, CreateInterfaceFn physicsFactory,
	CreateInterfaceFn fileSystemFactory, CGlobalVars* pGlobals)
{
	gpGlobals = pGlobals;
	return CServerGameDLL__DLLInit(thisptr, appSystemFactory, physicsFactory, fileSystemFactory, pGlobals);
}

//-----------------------------------------------------------------------------
// This is called when a new game is started. (restart, map)
//-----------------------------------------------------------------------------
bool CServerGameDLL::GameInit(void)
{
	const static int index = 1;
	return CallVFunc<bool>(index, this);
}

//-----------------------------------------------------------------------------
// This is called when scripts are getting recompiled. (restart, map, changelevel)
//-----------------------------------------------------------------------------
void CServerGameDLL::PrecompileScriptsJob(void)
{
	const static int index = 2;
	CallVFunc<void>(index, this);
}

//-----------------------------------------------------------------------------
// Called when a level is shutdown (including changing levels)
//-----------------------------------------------------------------------------
void CServerGameDLL::LevelShutdown(void)
{
	WeaponScriptVars_LevelShutdown();
	WeaponScriptVars_PhaseShift_LevelShutdown();
	WeaponScriptVars_WeaponLockedSet_LevelShutdown();
	WeaponScriptVars_InfiniteAmmo_LevelShutdown();
	WeaponHeat_LevelShutdown();
	GlobalNonRewind_LevelShutdown();
	DeathField_LevelShutdown();
	HighlightContext_LevelShutdown();
#ifndef DEDICATED
	ColorPalette_LevelShutdown();
	VScriptPlayer_LevelShutdown();
	Script_ClearRemoteFunctionRegistrations();
#endif

	const static int index = 8;
	CallVFunc<void>(index, this);
}

//-----------------------------------------------------------------------------
// This is called when a game ends (server disconnect, death, restart, load)
// NOT on level transitions within a game
//-----------------------------------------------------------------------------
void CServerGameDLL::GameShutdown(void)
{
	// Game just calls a nullsub for GameShutdown lol.
	const static int index = 9;
	CallVFunc<void>(index, this);
}

//-----------------------------------------------------------------------------
// Purpose: Gets the simulation tick interval
// Output : float
//-----------------------------------------------------------------------------
float CServerGameDLL::GetTickInterval(void)
{
	const static int index = 11;
	return CallVFunc<float>(index, this);
}

//-----------------------------------------------------------------------------
// Purpose: get all server classes
// Output : ServerClass*
//-----------------------------------------------------------------------------
ServerClass* CServerGameDLL::GetAllServerClasses(void)
{
	const static int index = 12;
	return CallVFunc<ServerClass*>(index, this);
}

static ConVar chat_debug("chat_debug", "0", FCVAR_RELEASE, "Enables chat-related debug printing.");
static ConVar sv_overrideTeamChatRestriction("sv_overrideTeamChatRestriction", "0", FCVAR_RELEASE,
	"When enabled this allows sv_forceChatToTeamOnly to take control of the team chat restriction.",
	"0: Default, 1: Forces the value from sv_forceChatToTeamOnly."
);
static ConVar sv_allowIconsInChat("sv_allowIconsInChat", "0", FCVAR_RELEASE, "Allow game icon characters in chat messages. 0 = Block icons, 1 = Allow icons");

// Function to check if a UTF-8 string contains blocked game icon characters
static bool SV_ContainsBlockedIcons(const char* text)
{
    if (!text) return false;
    
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
    
    while (*p)
    {
        // Check for UTF-8 sequences that represent the blocked icon characters
        // These characters are in the Private Use Area around U+F0000-U+F0FFF
        
        // UTF-8 encoding for U+F0000-U+F0FFF:
        // 4-byte sequence: 0xF3 0xB0 0x80-0xBF 0x80-0xBF
        if (p[0] == 0xF3 && p[1] == 0xB0)
        {
            // This is likely one of the blocked icon characters
            return true;
        }
        
        // Also check for some other common Private Use Area ranges that might contain icons
        // U+E000-U+F8FF (3-byte UTF-8: 0xEE-0xEF)
        if (p[0] >= 0xEE && p[0] <= 0xEF)
        {
            // Check if this matches the specific icon pattern
            // The icons you listed seem to be in a specific range
            if (p[0] == 0xEF && p[1] >= 0x80 && p[1] <= 0xBF)
            {
                return true; // Block these specific Private Use Area characters
            }
        }
        
        // Move to next character
        if (*p < 0x80)
        {
            // ASCII character
            p++;
        }
        else if ((*p & 0xE0) == 0xC0)
        {
            // 2-byte UTF-8
            p += 2;
        }
        else if ((*p & 0xF0) == 0xE0)
        {
            // 3-byte UTF-8
            p += 3;
        }
        else if ((*p & 0xF8) == 0xF0)
        {
            // 4-byte UTF-8
            p += 4;
        }
        else
        {
            // Invalid UTF-8, skip
            p++;
        }
    }
    
    return false;
}

void CServerGameDLL::OnReceivedSayTextMessage(CServerGameDLL* thisptr, int senderId, const char* text, bool isTeamChat)
{
	CPlayer* const pSenderPlayer = UTIL_PlayerByIndex(senderId);
	CClient* const pSenderClient = g_pServer->GetClient(senderId - 1);

	if (!pSenderPlayer || !pSenderClient ||  !pSenderPlayer->IsConnected())
		return;

	const bool bIsTeamChat = sv_overrideTeamChatRestriction.GetBool() ? sv_forceChatToTeamOnly->GetBool()  : isTeamChat;

	// Validate chat message characters
	if (text)
	{
		// Always check if it's valid UTF-8
		if (!V_IsValidUTF8(text))
		{
			if (chat_debug.GetBool())
				Msg(eDLL_T::SERVER, "Dropping chat message from '%s' (%llu): invalid UTF-8 encoding\n",
					pSenderPlayer->GetNetName(), pSenderPlayer->GetPlatformUserId());
			return; // Drop invalid UTF-8
		}
		
		// Check for blocked game icon characters (unless allowed)
		if (!sv_allowIconsInChat.GetBool() && SV_ContainsBlockedIcons(text))
		{
			if (chat_debug.GetBool())
				Msg(eDLL_T::SERVER, "Dropping chat message from '%s' (%llu): contains blocked game icon characters\n",
					pSenderPlayer->GetNetName(), pSenderPlayer->GetPlatformUserId());
			return; // Drop message with blocked icons
		}
		
		// Always check for control characters but allow normal Unicode
		for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
		{
			// Allow printable ASCII (32-126)
			if (*p >= 32 && *p <= 126)
				continue;
				
			// Allow high-bit characters (UTF-8 sequences) - this includes Japanese, Chinese, etc.
			if (*p >= 128)
			{
				// This is part of a UTF-8 sequence, allow it (Japanese, Chinese, Korean, etc.)
				continue;
			}
			
			// Block control characters (0-31, 127) except common whitespace
			if (*p != '\t' && *p != '\n' && *p != '\r')
			{
				if (chat_debug.GetBool())
					Msg(eDLL_T::SERVER, "Dropping chat message from '%s' (%llu): contains control characters\n",
						pSenderPlayer->GetNetName(), pSenderPlayer->GetPlatformUserId());
				return; // Drop message with control characters
			}
		}
	}
	const int nMaxClients = gpGlobals->maxClients;
	const bool bShouldApplyGlobalCommsMutes = SV_ShouldApplyTextChatGlobalMutes();

	pSenderPlayer->UpdateLastActiveTime(gpGlobals->curTime);
	
	const bool bSenderIsCommsBanned = pSenderClient->GetClientExtended()->IsClientCommsBanned();

	if (bShouldApplyGlobalCommsMutes && bSenderIsCommsBanned)
	{
		if (chat_debug.GetBool())
			Msg(eDLL_T::SERVER, "Dropping chat message from '%s' (%llu) User is globally muted", 
				pSenderPlayer->GetNetName(), pSenderPlayer->GetPlatformUserId());

		CSingleUserRecipientFilter filter(pSenderPlayer);
		filter.MakeReliable();

		v_UserMessageBegin(&filter, "SayText", 2);

		MessageWriteByte(pSenderPlayer->GetEdict());
		MessageWriteString(pSenderClient->GetClientExtended()->GetCommsMuteDisplayMessage());
		MessageWriteBool(bIsTeamChat);

		MessageEnd();
		return;
	}

	for (auto& cb : !PluginSystem()->GetChatMessageCallbacks())
	{
		if (!cb.Function()(pSenderPlayer, text ? text : "", sv_forceChatToTeamOnly->GetBool()))
		{
			if (chat_debug.GetBool())
			{
				char moduleName[MAX_PATH] = {};

				V_UnicodeToUTF8(V_UnqualifiedFileName(cb.ModuleName()), moduleName, MAX_PATH);

				Msg(eDLL_T::SERVER, "[%s] Plugin blocked chat message from '%s' (%llu): \"%s\"\n", moduleName, pSenderPlayer->GetNetName(), pSenderPlayer->GetPlatformUserId(), text ? text : "");
			}

			return;
		}
	}

	const bool bSenderDeadAndCanOnlyTalkToDead = hudchat_dead_can_only_talk_to_other_dead->GetBool() && pSenderPlayer->GetLifeState();

	for (int nRecipientIndex = 1; nRecipientIndex <= nMaxClients; nRecipientIndex++)
	{
		const CPlayer* const pRecipientPlayer = UTIL_PlayerByIndex(nRecipientIndex);
		const CClient* const pRecipientClient = g_pServer->GetClient(nRecipientIndex - 1);

		//Are we all there
		if (!pRecipientPlayer || !pRecipientClient || !pRecipientPlayer->IsConnected())
			continue;

		//If our recipient is banned and the host doesnt want banned people to see others chat skip them
		if (bShouldApplyGlobalCommsMutes && pRecipientClient->GetClientExtended()->IsClientCommsBanned() && !sv_commsBannedClientsCanReceiveComms.GetBool())
			continue;

		//If we are only allowed to talk to the dead make sure the recipient is dead
		if (bSenderDeadAndCanOnlyTalkToDead == !pRecipientPlayer->GetLifeState())
			continue;

		//If we arent the recipient
		if (pRecipientPlayer != pSenderPlayer &&
			//If the chat is limited to one team we must check the sender and recipient are on the same team
			bIsTeamChat && pSenderPlayer->GetTeamNum() != pRecipientPlayer->GetTeamNum()
		)
			continue;

		CSingleUserRecipientFilter filter(pRecipientPlayer);
		filter.MakeReliable();

		v_UserMessageBegin(&filter, "SayText", 2);

		MessageWriteByte(pSenderPlayer->GetEdict());
		MessageWriteString(text ? text : "");
		MessageWriteBool(bIsTeamChat);

		MessageEnd();
	}
}

static void DrawServerHitbox(int iEntity)
{
	const CEntInfo* const pInfo = g_serverEntityList->GetEntInfoPtrByIndex(iEntity);
	CBaseAnimating* const pAnimating = dynamic_cast<CBaseAnimating*>(pInfo->m_pEntity);

	if (pAnimating)
	{
		pAnimating->DrawServerHitboxes();
	}
}

static void DrawServerHitboxes()
{
	const int nVal = sv_showhitboxes->GetInt();
	Assert(nVal < NUM_ENT_ENTRIES);

	if (nVal == -1)
		return;

	if (nVal == 0)
	{
		for (int i = 0; i < NUM_ENT_ENTRIES; i++)
		{
			DrawServerHitbox(i);
		}
	}
	else // Lookup entity manually by index from 'sv_showhitboxes'.
	{
		DrawServerHitbox(nVal);
	}
}

static void DrawGeometryOverlays()
{
	const CEntInfo* pInfo = g_serverEntityList->FirstEntInfo();

	for (; pInfo; pInfo = pInfo->m_pNext)
	{
		CBaseEntity* const ent = (CBaseEntity*)pInfo->m_pEntity;

		if (ent->GetDebugOverlays() || ent->GetTimedOverlay())
		{
			ent->DrawDebugGeometryOverlays();
		}
	}
}

static void DrawAllDebugOverlays()
{
	if (!developer->GetBool())
		return;

	DrawServerHitboxes();
	DrawGeometryOverlays();
}

void CServerGameClients::_ProcessUserCmds(CServerGameClients* thisp, edict_t edict,
	bf_read* buf, int numCmds, int totalCmds, int droppedPackets, bool ignore, bool paused)
{
	int i;
	CUserCmd* from, * to;

	// We track last three command in case we drop some
	// packets but get them back.
	CUserCmd cmds[MAX_BACKUP_COMMANDS_PROCESS];
	CUserCmd cmdNull;  // For delta compression

	Assert(numCmds >= 0);
	Assert((totalCmds - numCmds) >= 0);

	// Too many commands?
	if (totalCmds < 0 || totalCmds >= (MAX_BACKUP_COMMANDS_PROCESS - 1) ||
		numCmds < 0 || numCmds > totalCmds)
	{
		const CClient* const pClient = g_pServer->GetClient(edict-1);

		Warning(eDLL_T::SERVER, "%s: Player '%s' sent too many cmds (%i)\n", __FUNCTION__, pClient->GetServerName(), totalCmds);
		buf->SetOverflowFlag();

		return;
	}

	from = &cmdNull;
	for (i = totalCmds - 1; i >= 0; i--)
	{
		to = &cmds[i];
		ReadUserCmd(buf, to, from);
		from = to;
	}

	// Server has gone inactive, just ignore.
	if (ignore)
	{
		return;
	}

	CPlayer* const pPlayer = UTIL_PlayerByIndex(edict);

	// Client not fully connected, just ignore.
	if (!pPlayer)
	{
		return;
	}

	pPlayer->ProcessUserCmds(cmds, numCmds, totalCmds, droppedPackets, paused);
}

//---------------------------------------------------------------------------------
// Purpose: dispatches the server frame job, this calls ExecuteFrameServerJob(),
//          anything you add in this function will either be before, or after the
//          server frame job has ran, so ThreadInServerFrameThread() will always
//          return false here. If you need to run code in the server frame thread,
//          consider adding your code in ExecuteFrameServerJob().
// Input  : flFrameTime - 
//			bRunOverlays - 
//			bUpdateFrame - 
//---------------------------------------------------------------------------------
static void DispatchFrameServerJob(double flFrameTime, bool bRunOverlays, bool bUniformUpdate)
{
	v_DispatchFrameServerJob(flFrameTime, bRunOverlays, bUniformUpdate);
}

//---------------------------------------------------------------------------------
// Purpose: executes the server frame job
// Input  : flFrameTime - 
//			bRunOverlays - 
//			bUpdateFrame - 
//---------------------------------------------------------------------------------
static void ExecuteFrameServerJob(double flFrameTime, bool bRunOverlays, bool bUpdateFrame)
{
	v_ExecuteFrameServerJob(flFrameTime, bRunOverlays, bUpdateFrame);

	LiveAPISystem()->RunFrame();
	DrawAllDebugOverlays();
}

void MessageEnd(void)
{
	Assert(*g_ppUsrMessageBuffer);

	g_pEngineServer->MessageEnd();

	(*g_ppUsrMessageBuffer) = nullptr;
}

void MessageWriteByte(int iValue)
{
	if (!*g_ppUsrMessageBuffer)
		Error(eDLL_T::ENGINE, EXIT_FAILURE, "WRITE_BYTE called with no active message\n");

	(*g_ppUsrMessageBuffer)->WriteByte(iValue);
}

void MessageWriteString(const char* pszString)
{
	if (!*g_ppUsrMessageBuffer)
		Error(eDLL_T::ENGINE, EXIT_FAILURE, "WriteString called with no active message\n");

	(*g_ppUsrMessageBuffer)->WriteString(pszString);
}

void MessageWriteBool(bool bValue)
{
	if (!*g_ppUsrMessageBuffer)
		Error(eDLL_T::ENGINE, EXIT_FAILURE, "WriteBool called with no active message\n");

	(*g_ppUsrMessageBuffer)->WriteOneBit(static_cast<int>(bValue));
}

void VServerGameDLL::Detour(const bool bAttach) const
{
	DetourSetup(&CServerGameDLL__DLLInit, &CServerGameDLL::DLLInit, bAttach);
	DetourSetup(&CServerGameDLL__OnReceivedSayTextMessage, &CServerGameDLL::OnReceivedSayTextMessage, bAttach);
	DetourSetup(&CServerGameClients__ProcessUserCmds, CServerGameClients::_ProcessUserCmds, bAttach);
	DetourSetup(&v_DispatchFrameServerJob, &DispatchFrameServerJob, bAttach);
	DetourSetup(&v_ExecuteFrameServerJob, &ExecuteFrameServerJob, bAttach);
}

CThreadMutex* g_serverFrameMutex;

CServerGameDLL* g_pServerGameDLL = nullptr;
CServerGameClients* g_pServerGameClients = nullptr;
CServerGameEnts* g_pServerGameEntities = nullptr;
CServerRandomStream* g_randomStream = nullptr;

// Holds global variables shared between engine and game.
CGlobalVars* gpGlobals = nullptr;
