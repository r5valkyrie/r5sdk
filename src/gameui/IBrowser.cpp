/******************************************************************************
-------------------------------------------------------------------------------
File   : IBrowser.cpp
Date   : 09:06:2021
Author : Sal
Purpose: Implements the in-game server browser front-end
-------------------------------------------------------------------------------
History:
- 09:06:2021   21:07 : Created by Sal
- 25:07:2021   14:26 : Implement private servers connect dialog and password field

******************************************************************************/

#include "core/stdafx.h"
#include "core/init.h"
#include "core/resource.h"
#include "tier0/fasttimer.h"
#include "tier0/frametask.h"
#include "tier0/commandline.h"
#include "windows/id3dx.h"
#include "windows/console.h"
#include "windows/resource.h"
#include "engine/net.h"
#include "engine/cmd.h"
#include "engine/cmodel_bsp.h"
#include "engine/host.h"
#include "engine/host_state.h"
#ifndef CLIENT_DLL
#include "engine/server/server.h"
#endif // CLIENT_DLL
#include "engine/client/clientstate.h"
#include "networksystem/serverlisting.h"
#include "networksystem/pylon.h"
#include "networksystem/hostmanager.h"
#include "networksystem/listmanager.h"
#include "rtech/playlists/playlists.h"
#include "common/callback.h"
#include "gameui/IBrowser.h"
#include "public/edict.h"
#include "game/shared/vscript_shared.h"
#include "game/server/gameinterface.h"
#include <engine/server/sv_main.cpp>
#include "pluginsystem/modsystem.h"

static ConCommand togglebrowser("togglebrowser", CBrowser::ToggleBrowser_f, "Show/hide the server browser", FCVAR_CLIENTDLL | FCVAR_RELEASE);

// Helper: check required mods against local enabled mods; returns false and a missing list if any missing
static bool Browser_HasRequiredMods(const NetGameServer_t& server, std::string& outMissing)
{
	outMissing.clear();
	if (server.requiredMods.empty())
		return true;
	if (!ModSystem()->IsEnabled())
	{
		// If mod system disabled, any required list means missing
		for (size_t i = 0; i < server.requiredMods.size(); ++i)
		{
			if (i) outMissing.append(", ");
			outMissing.append(server.requiredMods[i]);
		}
		return false;
	}

	CUtlVector<CUtlString> haveIds;
	CUtlVector<CUtlString> haveNormIds;
	ModSystem()->LockModList();
	const CUtlVector<CModSystem::ModInstance_t*>& mods = ModSystem()->GetModList();
	FOR_EACH_VEC(mods, i)
	{
		const CModSystem::ModInstance_t* mod = mods[i];
		if (!mod || !mod->IsEnabled())
			continue;
		haveIds.AddToTail(mod->id);
		haveNormIds.AddToTail(ModSystem()->GetNormalizedModID(mod));
	}
	ModSystem()->UnlockModList();

	auto eq = [](const CUtlString& a, const char* b)
	{
		return V_stricmp(a.String(), b) == 0;
	};

	CUtlVector<CUtlString> missing;
	for (const std::string& req : server.requiredMods)
	{
		bool found = false;
		FOR_EACH_VEC(haveIds, i)
		{
			if (eq(haveIds[i], req.c_str())) { found = true; break; }
		}
		if (!found)
		{
			FOR_EACH_VEC(haveNormIds, j)
			{
				if (eq(haveNormIds[j], req.c_str())) { found = true; break; }
			}
		}
		if (!found)
			missing.AddToTail(req.c_str());
	}

	if (missing.Count() > 0)
	{
		for (int i = 0; i < missing.Count(); ++i)
		{
			if (i) outMissing.append(", ");
			outMissing.append(missing[i].String());
		}
		return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CBrowser::CBrowser(void)
    : m_reclaimFocusOnTokenField(false)
    , m_queryNewListNonRecursive(false)
    , m_queryGlobalBanList(true)
    , m_lockedIconShaderResource(nullptr)
    , m_hostMessageColor(1.00f, 1.00f, 1.00f, 1.00f)
    , m_hiddenServerMessageColor(0.00f, 1.00f, 0.00f, 1.00f)
{
    m_surfaceLabel = "Server Browser";

    memset(m_serverTokenTextBuf, '\0', sizeof(m_serverTokenTextBuf));
    memset(m_serverAddressTextBuf, '\0', sizeof(m_serverAddressTextBuf));
    memset(m_serverNetKeyTextBuf, '\0', sizeof(m_serverNetKeyTextBuf));
    memset(m_serverPasswordTextBuf, '\0', sizeof(m_serverPasswordTextBuf));

    m_levelName = "mp_lobby";
    m_modeName = "dev_default";
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CBrowser::~CBrowser(void)
{
    Shutdown();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBrowser::Init(void)
{
    SetStyleVar();

    HMODULE sdkModule = reinterpret_cast<HMODULE>(g_SDKDll.GetModuleBase());
    m_lockedIconDataResource = GetModuleResource(sdkModule, IDB_PNG2);

    const bool ret = LoadTextureBuffer(reinterpret_cast<unsigned char*>(m_lockedIconDataResource.m_pData), int(m_lockedIconDataResource.m_nSize),
        &m_lockedIconShaderResource, &m_lockedIconDataResource.m_nWidth, &m_lockedIconDataResource.m_nHeight);

    IM_ASSERT(ret && m_lockedIconShaderResource);
    return ret;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBrowser::Shutdown(void)
{
    if (m_lockedIconShaderResource)
    {
        m_lockedIconShaderResource->Release();
    }
}

//-----------------------------------------------------------------------------
// Purpose: draws the main browser front-end
//-----------------------------------------------------------------------------
void CBrowser::RunFrame(void)
{
    // Uncomment these when adjusting the theme or layout.
    {
        //ImGui::ShowStyleEditor();
        //ImGui::ShowDemoWindow();
    }

    if (!m_initialized)
    {
        Init();
        m_initialized = true;
    }

    Animate();
    RunTask();

    int baseWindowStyleVars = 0;
    ImVec2 minBaseWindowRect;

    if (m_surfaceStyle == ImGuiStyle_t::MODERN)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 8.f, 10.f }); baseWindowStyleVars++;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_fadeAlpha);                 baseWindowStyleVars++;

        minBaseWindowRect = ImVec2(621.f, 532.f);
    }
    else
    {
        minBaseWindowRect = m_surfaceStyle == ImGuiStyle_t::LEGACY
            ? ImVec2(619.f, 526.f)
            : ImVec2(618.f, 524.f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 6.f, 6.f });  baseWindowStyleVars++;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_fadeAlpha);                 baseWindowStyleVars++;

        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);              baseWindowStyleVars++;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, minBaseWindowRect);       baseWindowStyleVars++;

    if (m_activated && m_reclaimFocus) // Reclaim focus on window apparition.
    {
        ImGui::SetNextWindowFocus();
        m_reclaimFocus = false;
    }

    DrawSurface();
    ImGui::PopStyleVar(baseWindowStyleVars);
}

//-----------------------------------------------------------------------------
// Purpose: runs tasks for the browser while not being drawn 
//-----------------------------------------------------------------------------
void CBrowser::RunTask()
{
    static bool bInitialized = false;
    static CFastTimer timer;

    if (!bInitialized)
    {
        timer.Start();
        bInitialized = true;
    }

    if (timer.GetDurationInProgress().GetSeconds() > pylon_host_update_interval.GetFloat())
    {
        UpdateHostingStatus();
        timer.Start();
    }

    if (m_activated)
    {
        if (m_queryNewListNonRecursive)
        {
            m_queryNewListNonRecursive = false;
            RefreshServerList();
        }
    }
    else // Refresh server list the next time 'm_activated' evaluates to true.
    {
        m_reclaimFocusOnTokenField = true;
        m_queryNewListNonRecursive = true;
    }
}

//-----------------------------------------------------------------------------
// Purpose: draws the server browser's main surface
// Output : true if a frame has been drawn, false otherwise
//-----------------------------------------------------------------------------
bool CBrowser::DrawSurface(void)
{
    if (!IsVisible())
        return false;

    if (!ImGui::Begin(m_surfaceLabel, &m_activated, ImGuiWindowFlags_None, &ResetInput))
    {
        ImGui::End();
        return false;
    }

    SetRect(927.f, 524.f, 50, 50.f);

    if (ImGui::BeginTabBar("CompMenu##ServerBrowser_DrawSurface"))
    {
        if (ImGui::BeginTabItem("Browsing##ServerBrowser_DrawSurface"))
        {
            DrawBrowserPanel();
            ImGui::EndTabItem();
        }
#ifndef CLIENT_DLL
        if (ImGui::BeginTabItem("Hosting##ServerBrowser_DrawSurface"))
        {
            DrawHostPanel();
            ImGui::EndTabItem();
        }
#endif // !CLIENT_DLL

        ImGui::EndTabBar();
    }

    ImGui::End();

    return true;
}

static char s_passwordBuf[128] = { 0 };

//-----------------------------------------------------------------------------
// Purpose: draws the server browser section
//-----------------------------------------------------------------------------
void CBrowser::DrawBrowserPanel(void)
{
    ImGui::BeginGroup();
    m_serverBrowserTextFilter.Draw();
    ImGui::SameLine();

    if (ImGui::Button("Refresh##ServerBrowser_DrawBrowserPanel"))
    {
        m_serverListMessage.clear();
        RefreshServerList();
    }

    ImGui::EndGroup();
    ImGui::TextColored(ImVec4(1.00f, 0.00f, 0.00f, 1.00f), "%s", m_serverListMessage.c_str());
    ImGui::Separator();

    int frameStyleVars = 0; // Eliminate borders around server list table.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 1.f, 0.f });  frameStyleVars++;

    const float fFooterHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();

    if (ImGui::BeginTable("##ServerBrowser_DrawBrowserPanel_ServerListTable", 8, ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, { 0, -fFooterHeight }))
    {
        if (m_surfaceStyle == ImGuiStyle_t::MODERN)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 8.f, 0.f }); frameStyleVars++;
        }
        else
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4.f, 0.f }); frameStyleVars++;
        }

        ImGui::TableSetupColumn(" ", ImGuiTableColumnFlags_WidthFixed, 12); // Lock icon
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 25);
        ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthStretch, 20);
        ImGui::TableSetupColumn("Playlist", ImGuiTableColumnFlags_WidthStretch, 10);
        ImGui::TableSetupColumn("Required Mods", ImGuiTableColumnFlags_WidthStretch, 15);
        ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthStretch, 5);
        ImGui::TableSetupColumn("Port", ImGuiTableColumnFlags_WidthStretch, 5);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 5);

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        g_ServerListManager.m_Mutex.Lock();
        vector<const NetGameServer_t*> filteredServers;

        // Filter the server list first before running it over the ImGui list
        // clipper, if we do this within the clipper, clipper.Step() will fail
        // as the calculation for the remainder will be off.
        for (int i = 0; i < g_ServerListManager.m_vServerList.size(); i++)
        {
            const NetGameServer_t& server = g_ServerListManager.m_vServerList[i];

            const char* pszHostName = server.name.c_str();
            const char* pszHostMap = server.map.c_str();
            const char* pszPlaylist = server.playlist.c_str();

            if (m_serverBrowserTextFilter.PassFilter(pszHostName, &pszHostName[server.name.length()])
                || m_serverBrowserTextFilter.PassFilter(pszHostMap, &pszHostMap[server.map.length()])
                || m_serverBrowserTextFilter.PassFilter(pszPlaylist, &pszPlaylist[server.playlist.length()]))
            {
                filteredServers.push_back(&server);
            }
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filteredServers.size()));

        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
            {
                const NetGameServer_t* const server = filteredServers[i];
                const ImGuiTextFlags textFlags = ImGuiTextFlags_NoWidthForLargeClippedText;

                // Lock icon column
                ImGui::TableNextColumn();
                if (server->hasPassword && m_lockedIconShaderResource)
                {
                    ImGui::Image((ImTextureID)(intptr_t)m_lockedIconShaderResource, ImVec2(12.f, 12.f));
                }

                // Name column
                ImGui::TableNextColumn();
                const char* const pszHostName = server->name.c_str();
                const bool tintLocked = server->hasPassword;
                if (tintLocked)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.85f, 0.00f, 1.00f));
                ImGui::TextEx(pszHostName, &pszHostName[server->name.length()], textFlags);
                if (tintLocked)
                    ImGui::PopStyleColor();

                ImGui::TableNextColumn();

                const char* const pszHostMap = server->map.c_str();
                ImGui::TextEx(pszHostMap, &pszHostMap[server->map.length()], textFlags);

                ImGui::TableNextColumn();

                const char* const pszPlaylist = server->playlist.c_str();
                ImGui::TextEx(pszPlaylist, &pszPlaylist[server->playlist.length()], textFlags);

                // Mods column
                ImGui::TableNextColumn();
                if (!server->requiredMods.empty())
                {
                    std::string modsJoined;
                    modsJoined.reserve(64);
                    for (size_t m = 0; m < server->requiredMods.size(); ++m)
                    {
                        if (m) modsJoined.append(", ");
                        modsJoined.append(server->requiredMods[m]);
                    }
                    const char* const pszMods = modsJoined.c_str();
                    ImGui::TextEx(pszMods, &pszMods[modsJoined.length()], textFlags);
                }
                else
                {
                    ImGui::TextUnformatted("-");
                }

                ImGui::TableNextColumn();

                const std::string playerNums = Format("%3d/%3d", server->numPlayers, server->maxPlayers);

                const char* const pszPlayerNums = playerNums.c_str();
                ImGui::TextEx(pszPlayerNums, &pszPlayerNums[playerNums.length()], textFlags);

                ImGui::TableNextColumn();
                ImGui::Text("%d", server->port);

                ImGui::TableNextColumn();
                ImGui::PushID(i);

                const std::string popupName = Format("Enter Password##popup_row_%d", i);
                const bool isLocked = server->hasPassword;

                if (ImGui::Button(isLocked ? "Enter password" : "Connect"))
                {
                    if (isLocked)
                    {
                        s_passwordBuf[0] = '\0';
                        ImGui::OpenPopup(popupName.c_str());
                    }
                    else
                    {
                        std::string missing;
                        if (!Browser_HasRequiredMods(*server, missing))
                        {
                            m_serverListMessage = Format("Missing required mods: %s", missing.c_str());
                        }
                        else
                        {
                            g_ServerListManager.ConnectToServer(server->address, server->port, server->netKey, "");
                        }
                    }
                }

                // Draw the modal if opened
                if (ImGui::BeginPopupModal(popupName.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::Text("This server requires a password.");
                    ImGui::Spacing();
                    ImGui::InputText("Password", s_passwordBuf, IM_ARRAYSIZE(s_passwordBuf), ImGuiInputTextFlags_Password);

                    ImGui::Spacing();
                    if (ImGui::Button("Connect", ImVec2(120, 0)))
                    {
                        std::string missing;
                        if (!Browser_HasRequiredMods(*server, missing))
                        {
                            m_serverListMessage = Format("Missing required mods: %s", missing.c_str());
                        }
                        else
                        {
                            g_ServerListManager.ConnectToServer(server->address, server->port, server->netKey, s_passwordBuf);
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    {
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
        }

        filteredServers.clear();
        g_ServerListManager.m_Mutex.Unlock();

        ImGui::PopStyleVar(frameStyleVars);
        ImGui::EndTable();
    }
    else
    {
        ImGui::PopStyleVar(frameStyleVars);
    }

    ImGui::Separator();

    const ImVec2 regionAvail = ImGui::GetContentRegionAvail();
    const ImGuiStyle& style = ImGui::GetStyle();

    // 4 elements means 3 spacings between items, this has to be subtracted
    // from the remaining available region to get correct results on all
    // window padding values!!!
    const float itemWidth = (regionAvail.x - (3 * style.ItemSpacing.x)) / 4;

    ImGui::PushItemWidth(itemWidth);
    {
        ImGui::InputTextWithHint("##ServerBrowser_DrawBrowserPanel_ServerAddress", "Server address and port", m_serverAddressTextBuf, sizeof(m_serverAddressTextBuf));

        ImGui::SameLine();
        ImGui::InputTextWithHint("##ServerBrowser_DrawBrowserPanel_ServerKey", "Encryption key", m_serverNetKeyTextBuf, sizeof(m_serverNetKeyTextBuf));

        ImGui::SameLine();
        ImGui::InputTextWithHint("##ServerBrowser_DrawBrowserPanel_ServerPassword", "Server Password", m_serverPasswordTextBuf, sizeof(m_serverPasswordTextBuf));

        ImGui::SameLine();
        if (ImGui::Button("Connect##ServerBrowser_DrawBrowserPanel_ServerConnect", ImVec2(itemWidth, ImGui::GetFrameHeight())))
        {
            if (m_serverAddressTextBuf[0])
            {
                g_ServerListManager.ConnectToServer(m_serverAddressTextBuf, m_serverNetKeyTextBuf, m_serverPasswordTextBuf);
            }
        }

        ImGui::SameLine();

        // NOTE: -9 to prevent the last button from clipping/colliding with the
        // window drag handle! -9 makes the distance between the handle and the
        // last button equal as that of the developer console.
        if (ImGui::Button("Private servers##ServerBrowser_DrawBrowserPanel", ImVec2(itemWidth - 9, ImGui::GetFrameHeight())))
        {
            ImGui::OpenPopup("Private Server##ServerBrowser_HiddenServersModal");
        }

        HiddenServersModal();
    }

    ImGui::PopItemWidth();
}

//-----------------------------------------------------------------------------
// Purpose: refreshes the server browser list with available servers
//-----------------------------------------------------------------------------
void CBrowser::RefreshServerList(void)
{
    Msg(eDLL_T::CLIENT, "Refreshing server list with matchmaking host '%s'\n", pylon_matchmaking_hostname.GetString());

    // Thread the request, and let the main thread assign status message back
    std::thread request([&]
        {
            std::string serverListMessage;
            size_t numServers;
            g_ServerListManager.RefreshServerList(serverListMessage, numServers);

            g_TaskQueue.Dispatch([&, serverListMessage]
                {
                    SetServerListMessage(serverListMessage.c_str());
                }, 0);
        }
    );

    request.detach();
}

//-----------------------------------------------------------------------------
// Purpose: draws the hidden private server modal
//-----------------------------------------------------------------------------
void CBrowser::HiddenServersModal(void)
{
    float modalWindowHeight; // Set the padding accordingly for each theme.
    switch (m_surfaceStyle)
    {
    case ImGuiStyle_t::LEGACY:
        modalWindowHeight = 207.f;
        break;
    case ImGuiStyle_t::MODERN:
        modalWindowHeight = 214.f;
        break;
    default:
        modalWindowHeight = 206.f;
        break;
    }

    int modalStyleVars = 0;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(408.f, modalWindowHeight));    modalStyleVars++;

    bool isModalStillOpen = true;
    if (ImGui::BeginPopupModal("Private Server##ServerBrowser_HiddenServersModal", &isModalStillOpen, ImGuiWindowFlags_NoResize))
    {
        ImGui::SetWindowSize(ImVec2(408.f, modalWindowHeight), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.00f, 0.00f, 0.00f, 0.00f)); // Override the style color for child bg.

        ImGui::BeginChild("##ServerBrowser_HiddenServersModal_IconParent", ImVec2(float(m_lockedIconDataResource.m_nWidth), float(m_lockedIconDataResource.m_nHeight)));
        ImGui::Image((ImTextureID)(intptr_t)m_lockedIconShaderResource, ImVec2(float(m_lockedIconDataResource.m_nWidth), float(m_lockedIconDataResource.m_nHeight))); // Display texture.
        ImGui::EndChild();

        ImGui::PopStyleColor(); // Pop the override for the child bg.

        ImGui::SameLine();
        ImGui::Text("Enter token to connect");

        const ImVec2 contentRegionMax = ImGui::GetContentRegionAvail();
        ImGui::PushItemWidth(contentRegionMax.x); // Override item width.

        const bool hitEnter = ImGui::InputTextWithHint("##ServerBrowser_HiddenServersModal_TokenInput", "Token (required)", 
            m_serverTokenTextBuf, sizeof(m_serverTokenTextBuf), ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::PopItemWidth();

        if (m_reclaimFocusOnTokenField)
        {
            ImGui::SetKeyboardFocusHere(-1); // -1 means previous widget.
            m_reclaimFocusOnTokenField = false;
        }

        ImGui::Dummy(ImVec2(contentRegionMax.x, 19.f)); // Place a dummy, basically making space inserting a blank element.

        ImGui::TextColored(m_hiddenServerMessageColor, "%s", m_hiddenServerRequestMessage.c_str());
        ImGui::Separator();

        if (ImGui::Button("Connect##ServerBrowser_HiddenServersModal", ImVec2(contentRegionMax.x, 24)) || hitEnter)
        {
            m_hiddenServerRequestMessage.clear();
            m_reclaimFocusOnTokenField = true;

            if (m_serverTokenTextBuf[0])
            {
                NetGameServer_t server;
                const bool result = g_MasterServer.GetServerByToken(server, m_hiddenServerRequestMessage, m_serverTokenTextBuf); // Send token connect request.

                if (result && !server.name.empty())
                {
                    std::string missing;
                    if (!Browser_HasRequiredMods(server, missing))
                    {
                        m_hiddenServerRequestMessage = Format("Missing required mods: %s", missing.c_str());
                        m_hiddenServerMessageColor = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
                    }
                    else
                    {
                        g_ServerListManager.ConnectToServer(server.address, server.port, server.netKey, ""); // Connect to the server
                        m_hiddenServerRequestMessage = Format("Found server: %s", server.name.c_str());
                        m_hiddenServerMessageColor = ImVec4(0.00f, 1.00f, 0.00f, 1.00f);
                        ImGui::CloseCurrentPopup();
                    }
                }
                else
                {
                    if (m_hiddenServerRequestMessage.empty())
                    {
                        m_hiddenServerRequestMessage = "Unknown error.";
                    }
                    else // Display error message.
                    {
                        m_hiddenServerRequestMessage = Format("Error: %s", m_hiddenServerRequestMessage.c_str());
                    }
                    m_hiddenServerMessageColor = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
                }
            }
            else
            {
                m_hiddenServerRequestMessage = "Token is required.";
                m_hiddenServerMessageColor = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
            }
        }

        if (ImGui::Button("Close##ServerBrowser_HiddenServersModal", ImVec2(contentRegionMax.x, 24)))
        {
            m_hiddenServerRequestMessage.clear();
            m_reclaimFocusOnTokenField = true;

            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
        ImGui::PopStyleVar(modalStyleVars);
    }
    else if (!isModalStillOpen)
    {
        m_hiddenServerRequestMessage.clear();
        m_reclaimFocusOnTokenField = true;

        ImGui::PopStyleVar(modalStyleVars);
    }
    else
    {
        ImGui::PopStyleVar(modalStyleVars);
    }
}

void CBrowser::HandleInvalidFields(const bool offline)
{
    if (!offline && m_serverName.empty())
    {
        m_hostMessage = "Server name is required.";
        m_hostMessageColor = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    }
    else if (m_levelName.empty())
    {
        m_hostMessage = "Level name is required.";
        m_hostMessageColor = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    }
    else if (m_modeName.empty())
    {
        m_hostMessage = "Mode name is required.";
        m_hostMessageColor = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    }
}

static char s_hostPassword[128] = {};
static bool s_pwInited = false;

//-----------------------------------------------------------------------------
// Purpose: draws the host section
//-----------------------------------------------------------------------------
void CBrowser::DrawHostPanel(void)
{
#ifndef CLIENT_DLL
    if (ImGui::InputTextWithHint("##ServerBrowser_DrawHostPanel_ServerName", "Server name (required)", &m_serverName))
    {
        hostname->SetValue(m_serverName.c_str());
    }
    
    if (ImGui::InputTextWithHint("##ServerBrowser_DrawHostPanel_ServerDescription", "Server description (optional)", &m_serverDescription))
    {
        hostdesc.SetValue(m_serverDescription.c_str());
    }

    ImGui::Spacing();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f)); // Make drop down borders consistent.

    if (ImGui::BeginCombo("Map##ServerBrowser_DrawHostPanel", m_levelName.c_str()))
    {
        g_InstalledMapsMutex.Lock();

        for (const CUtlString& mapName : g_InstalledMaps)
        {
            const char* const cachedMapName = mapName.String();

            if (ImGui::Selectable(cachedMapName,
                mapName.IsEqual_CaseInsensitive(m_levelName.c_str())))
            {
                m_levelName = cachedMapName;
            }
        }

        g_InstalledMapsMutex.Unlock();
        ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("Mode##ServerBrowser_DrawHostPanel", m_modeName.c_str()))
    {
        for (const CUtlString& playlist : g_vecAllPlaylists)
        {
            const char* const cachedPlaylists = playlist.String();

            if (ImGui::Selectable(cachedPlaylists,
                playlist.IsEqual_CaseInsensitive(m_modeName.c_str())))
            {
                m_modeName = cachedPlaylists;
            }
        }

        ImGui::EndCombo();
    }

    ImGui::PopStyleVar();

    m_queryGlobalBanList = sv_globalBanlist.GetBool(); // Sync toggle with 'sv_globalBanlist'.
    if (ImGui::Checkbox("Load global banned list##ServerBrowser_DrawHostPanel", &m_queryGlobalBanList))
    {
        sv_globalBanlist.SetValue(m_queryGlobalBanList);
    }

    ImGui::Text("Server visibility");

    if (ImGui::SameLine(); ImGui::RadioButton("offline##ServerBrowser_DrawHostPanel", pylon_host_visibility.GetInt() == ServerVisibility_e::OFFLINE))
    {
        pylon_host_visibility.SetValue(ServerVisibility_e::OFFLINE);
    }
    if (ImGui::SameLine(); ImGui::RadioButton("hidden##ServerBrowser_DrawHostPanel", pylon_host_visibility.GetInt() == ServerVisibility_e::HIDDEN))
    {
        pylon_host_visibility.SetValue(ServerVisibility_e::HIDDEN);
    }
    if (ImGui::SameLine(); ImGui::RadioButton("public##ServerBrowser_DrawHostPanel", pylon_host_visibility.GetInt() == ServerVisibility_e::PUBLIC))
    {
        pylon_host_visibility.SetValue(ServerVisibility_e::PUBLIC);
    }

    if ((pylon_host_visibility.GetInt() == PUBLIC))
    {
        if (!s_pwInited)
        {
            if (ConVar* cvPw = g_pCVar->FindVar("sv_password"))
                Q_strncpy(s_hostPassword, cvPw->GetString(), sizeof(s_hostPassword));
            else
                s_hostPassword[0] = '\0';
            s_pwInited = true;
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Server Password");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
        if (ImGui::InputText("##host_password", s_hostPassword, IM_ARRAYSIZE(s_hostPassword), ImGuiInputTextFlags_Password))
        {
            if (ConVar* cvPw = g_pCVar->FindVar("sv_password")) cvPw->SetValue(s_hostPassword);
        }

        // Update sv_password when editing finishes
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            if (ConVar* cvPw = g_pCVar->FindVar("sv_password"))
                cvPw->SetValue(s_hostPassword);
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            s_hostPassword[0] = '\0';
            if (ConVar* cvPw = g_pCVar->FindVar("sv_password"))
                cvPw->SetValue("");
        }

        ImGui::TextDisabled("Leave blank for no password");
    }
    else
    {
        // Reset init state when switching away from Public so we re-pull sv_password next time
        s_pwInited = false;

        // Optional: automatically clear password when not public
        // if (ConVar* cvPw = g_pCVar->FindVar("sv_password")) cvPw->SetValue("");
    }

    ImGui::TextColored(m_hostMessageColor, "%s", m_hostMessage.c_str());
    if (!m_hostToken.empty())
    {
        ImGui::InputText("##ServerBrowser_DrawHostPanel_HostToken", &m_hostToken, ImGuiInputTextFlags_ReadOnly);
    }

    ImGui::Spacing();

    const ImVec2 contentRegionMax = ImGui::GetContentRegionAvail();

    const bool serverActive = g_pServer->IsActive();
    const bool clientActive = g_pClientState->IsActive();

    const bool isOffline = pylon_host_visibility.GetInt() == (int)ServerVisibility_e::OFFLINE;
    const bool hasName = isOffline ? true : !m_serverName.empty();

    if (!g_pHostState->m_bActiveGame)
    {
        if (ImGui::Button("Start server##ServerBrowser_DrawHostPanel", ImVec2(contentRegionMax.x, 32)))
        {
            m_hostMessage.clear();

            if (hasName && !m_levelName.empty() && !m_modeName.empty())
            {
                g_ServerHostManager.LaunchServer(m_levelName.c_str(), m_modeName.c_str()); // Launch server.
            }
            else
            {
                HandleInvalidFields(isOffline);
            }
        }

        if (ImGui::Button("Reload playlist##ServerBrowser_DrawHostPanel", ImVec2(contentRegionMax.x, 32)))
        {
            v_Playlists_Download_f();
            Playlists_SDKInit(); // Re-Init playlist.
        }

        if (ImGui::Button("Reload banlist##ServerBrowser_DrawHostPanel", ImVec2(contentRegionMax.x, 32)))
        {
            g_BanSystem.Clear();
            g_BanSystem.LoadList();
        }
    }
    else
    {
        if (ImGui::Button("Stop server##ServerBrowser_DrawHostPanel", ImVec2(contentRegionMax.x, 32)))
        {
            ProcessCommand("LeaveMatch"); // TODO: use script callback instead.
            g_pHostState->m_iNextState = HostStates_t::HS_GAME_SHUTDOWN;
        }

        if (ImGui::Button("Change level##ServerBrowser_DrawHostPanel", ImVec2(contentRegionMax.x, 32)))
        {
            if (!m_levelName.empty() && !m_modeName.empty())
            {
                g_ServerHostManager.ChangeLevel(m_levelName.c_str(), m_modeName.c_str());
            }
            else
            {
                HandleInvalidFields(isOffline);

                m_hostMessage = "Failed to change level: 'levelname' was empty.";
                m_hostMessageColor = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
            }
        }

        if (serverActive)
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Rebuild AI network##ServerBrowser_DrawHostPanel", ImVec2(contentRegionMax.x, 32)))
            {
                ProcessCommand("BuildAINFile");
            }

            if (ImGui::Button("Reload NavMesh##ServerBrowser_DrawHostPanel", ImVec2(contentRegionMax.x, 32)))
            {
                ProcessCommand("navmesh_hotswap");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Reparse AI settings##ServerBrowser_DrawHostPanel", ImVec2(contentRegionMax.x, 32)))
            {
                Msg(eDLL_T::ENGINE, "Reparsing AI data on %s\n", clientActive ? "server and client" : "server");
                ProcessCommand("aisettings_reparse");

                if (g_pClientState->IsActive())
                {
                    ProcessCommand("aisettings_reparse_client");
                }
            }

            if (ImGui::Button("Reparse Weapon settings##ServerBrowser_DrawHostPanel", ImVec2(contentRegionMax.x, 32)))
            {
                Msg(eDLL_T::ENGINE, "Reparsing weapon data on %s\n", clientActive ? "server and client" : "server");
                ProcessCommand("weapon_reparse");
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Reparse all scripts##ServerBrowser_DrawHostPanel", ImVec2(contentRegionMax.x, 32)))
    {
        Msg(eDLL_T::ENGINE, "Reparsing all scripts on %s\n", serverActive ? "server and client" : "client");
        Host_ReparseAllScripts();
    }
#endif // !CLIENT_DLL
}

//-----------------------------------------------------------------------------
// Purpose: updates the host status
//-----------------------------------------------------------------------------
void CBrowser::UpdateHostingStatus(void)
{
#ifndef CLIENT_DLL
    assert(g_pHostState && g_pCVar);

    const HostStatus_e hostStatus = (pylon_host_visibility.GetInt() != ServerVisibility_e::OFFLINE && g_pServer->IsActive())
        ? HostStatus_e::HOSTING 
        : HostStatus_e::NOT_HOSTING;

    g_ServerHostManager.SetHostStatus(hostStatus); // Are we hosting a server?

    switch (hostStatus)
    {
    case HostStatus_e::NOT_HOSTING:
    {
        if (!m_hostToken.empty())
        {
            m_hostToken.clear();
        }

        if (ImGui::ColorConvertFloat4ToU32(m_hostMessageColor) == // Only clear if this is green (a valid hosting message).
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.00f, 1.00f, 0.00f, 1.00f)))
        {
            m_hostMessage.clear();
            m_hostMessageColor = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        }

        break;
    }
    case HostStatus_e::HOSTING:
    {
        if (*g_nServerRemoteChecksum == NULL) // Check if script checksum is valid yet.
        {
            break;
        }

        const ServerVisibility_e serverVisibility = (ServerVisibility_e)pylon_host_visibility.GetInt();

        if (serverVisibility == ServerVisibility_e::OFFLINE)
        {
            break;
        }

        const char* pw = "";
        if (ConVar* const pPw = g_pCVar->FindVar("sv_password"))
            pw = pPw->GetString();

        string password = pw;

        NetGameServer_t netGameServer
        {
            hostname->GetString(),
            hostdesc.GetString(),
            pylon_host_visibility.GetInt() == ServerVisibility_e::HIDDEN,
            password.length() > 0,
            g_pHostState->m_levelName,
            v_Playlists_GetCurrent(),
            hostip->GetString(),
            hostport->GetInt(),
            g_pNetKey->GetBase64NetKey(),
            password,
            *g_nServerRemoteChecksum,
            SDK_VERSION,
            g_pServer->GetNumClients(),
            gpGlobals->maxClients,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
                ).count()
        };

        // Populate required mods from ModSystem
        if (ModSystem()->IsEnabled())
        {
            ModSystem()->LockModList();
            const CUtlVector<CUtlString>& req = ModSystem()->GetRequiredMods();
            for (int i = 0; i < req.Count(); ++i)
            {
                netGameServer.requiredMods.emplace_back(req[i].String());
            }
            ModSystem()->UnlockModList();
        }

        // Set mods profile
        netGameServer.modsProfile = sv_modsProfile.GetString();

        SendHostingPostRequest(netGameServer);
        break;
    }
    default:
        break;
    }
#endif // !CLIENT_DLL
}

//-----------------------------------------------------------------------------
// Purpose: sends the hosting POST request to the comp server and installs the
//          host data on the server browser
// Input  : &gameServer - 
//-----------------------------------------------------------------------------
void CBrowser::SendHostingPostRequest(NetGameServer_t& gameServer)
{
#ifndef CLIENT_DLL
    std::thread request([&, gameServer = std::move(gameServer)]
        {
            string hostRequestMessage;
            string hostToken;
            string hostIp;

            const bool result = g_MasterServer.PostServerHost(hostRequestMessage, hostToken, hostIp, gameServer);

            g_TaskQueue.Dispatch([&, result, hostRequestMessage, hostToken, hostIp]
                {
                    InstallHostingDetails(result, hostRequestMessage, hostToken, hostIp);
                }, 0);
        }
    );
    request.detach();
#endif // !CLIENT_DLL
}

//-----------------------------------------------------------------------------
// Purpose: installs the host data on the server browser
// Input  : postFailed - 
//          *hostMessage - 
//          *hostToken - 
//          &hostIp - 
//-----------------------------------------------------------------------------
void CBrowser::InstallHostingDetails(const bool postFailed, const string& hostMessage, const string& hostToken, const string& hostIp)
{
#ifndef CLIENT_DLL
    m_hostMessage = hostMessage;
    m_hostToken = hostToken;

    if (!hostIp.empty())
    {
        g_ServerHostManager.SetHostIP(hostIp);
    }

    if (postFailed)
    {
        m_hostMessageColor = ImVec4(0.00f, 1.00f, 0.00f, 1.00f);

        m_hostMessage = m_hostToken.empty()
            ? "Broadcasting"
            : "Broadcasting: share the following token for clients to connect: ";
    }
    else
    {
        m_hostMessageColor = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    }
#endif // !CLIENT_DLL
}

//-----------------------------------------------------------------------------
// Purpose: processes submitted commands
// Input  : *pszCommand - 
//-----------------------------------------------------------------------------
void CBrowser::ProcessCommand(const char* pszCommand) const
{
    Cbuf_AddText(Cbuf_GetCurrentPlayer(), pszCommand, cmd_source_t::kCommandSrcCode);
}

//-----------------------------------------------------------------------------
// Purpose: toggles the server browser
//-----------------------------------------------------------------------------
void CBrowser::ToggleBrowser_f()
{
    g_Browser.m_activated ^= true;
    ResetInput(); // Disable input to game when browser is drawn.
}

CBrowser g_Browser;
