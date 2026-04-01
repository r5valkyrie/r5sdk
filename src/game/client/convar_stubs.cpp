//=============================================================================//
//
// Purpose: ConVar stubs for script compatibility
//
// Scripts reference these ConVars via GetConVarInt/GetConVarString/etc.
// If the ConVar doesn't exist, the script call fails or returns wrong values.
// These stubs ensure the ConVars exist with safe defaults.
//
//=============================================================================//

#include "core/stdafx.h"
#include "tier1/convar.h"

// Forces the linker to include this object file. Without an exported symbol,
// the static ConVar constructors would be stripped from the final binary.
void ConVarStubs_Init() {}

//-----------------------------------------------------------------------------
// Matchmaking / Party
//-----------------------------------------------------------------------------
static ConVar match_jip("match_jip", "0", FCVAR_RELEASE, "Join in progress.");
static ConVar match_roleToken("match_roleToken", "", FCVAR_RELEASE, "Match role token.");
static ConVar party_nofill_selected("party_nofill_selected", "0", FCVAR_RELEASE, "Party no-fill preference.");
static ConVar orientation_matches_disabled("orientation_matches_disabled", "0", FCVAR_RELEASE, "Disable orientation matches.");
static ConVar skip_training("skip_training", "1", FCVAR_RELEASE, "Skip training on first launch.");
static ConVar ftue_flow_enabled("ftue_flow_enabled", "0", FCVAR_RELEASE, "Enable first-time user experience flow.");

//-----------------------------------------------------------------------------
// Custom Match
//-----------------------------------------------------------------------------
static ConVar customMatch_public_enabled("customMatch_public_enabled", "0", FCVAR_RELEASE, "Enable public custom matches.");
static ConVar customMatch_playerToken("customMatch_playerToken", "", FCVAR_RELEASE, "Custom match player token.");
static ConVar customMatch_startMatchmakingDelay("customMatch_startMatchmakingDelay", "10", FCVAR_RELEASE, "Custom match start delay.");

//-----------------------------------------------------------------------------
// Ranked
//-----------------------------------------------------------------------------
static ConVar ranked_disable_placement_matches("ranked_disable_placement_matches", "0", FCVAR_RELEASE, "Disable ranked placement matches.");
static ConVar ranked_disable_point_gain("ranked_disable_point_gain", "0", FCVAR_RELEASE, "Disable ranked point gain.");
static ConVar ranked_current_period_start_score("ranked_current_period_start_score", "0", FCVAR_RELEASE, "Ranked period start score.");
static ConVar ranked_disable_promo_trials("ranked_disable_promo_trials", "0", FCVAR_RELEASE, "Disable ranked promo trials.");

//-----------------------------------------------------------------------------
// Store / MTX (microtransactions) -- all disabled/safe defaults
//-----------------------------------------------------------------------------
static ConVar mtx_hardenDirtyOffers("mtx_hardenDirtyOffers", "0", FCVAR_RELEASE, "Harden dirty offer validation.");
static ConVar mtx_allowClearUnackedRewards("mtx_allowClearUnackedRewards", "0", FCVAR_RELEASE, "Allow clearing unacked rewards.");
static ConVar mtx_allow_pending_grants_for_item_ownership("mtx_allow_pending_grants_for_item_ownership", "0", FCVAR_RELEASE, "Allow pending grants for item ownership.");
static ConVar mtx_escrow_trusted_level("mtx_escrow_trusted_level", "0", FCVAR_RELEASE, "Escrow trusted level threshold.");
static ConVar mtx_store_sections_enabled("mtx_store_sections_enabled", "0", FCVAR_RELEASE, "Enable store sections.");
static ConVar mtx_allowPersistenceBadgeConversion("mtx_allowPersistenceBadgeConversion", "0", FCVAR_RELEASE, "Allow persistence badge conversion.");
static ConVar mtx_purchaseBPLevelsWithLegendTokensEnabled("mtx_purchaseBPLevelsWithLegendTokensEnabled", "0", FCVAR_RELEASE, "Enable BP level purchase with legend tokens.");
static ConVar mtx_purchaseBPLevelsWithLegendTokensEndDaysOffset("mtx_purchaseBPLevelsWithLegendTokensEndDaysOffset", "0", FCVAR_RELEASE, "BP purchase end offset.");
static ConVar mtx_purchaseBPLevelsWithLegendTokensLimit("mtx_purchaseBPLevelsWithLegendTokensLimit", "0", FCVAR_RELEASE, "BP purchase token limit.");
static ConVar mtx_purchaseBPLevelsWithLegendTokensMinLevel("mtx_purchaseBPLevelsWithLegendTokensMinLevel", "0", FCVAR_RELEASE, "BP purchase minimum level.");
static ConVar mtx_purchaseBPLevelsWithLegendTokensStartDaysOffset("mtx_purchaseBPLevelsWithLegendTokensStartDaysOffset", "0", FCVAR_RELEASE, "BP purchase start offset.");
static ConVar client_boostcount("client_boostcount", "0", FCVAR_RELEASE, "Client boost count.");
static ConVar escrow_is_player_trusted("escrow_is_player_trusted", "0", FCVAR_RELEASE, "Escrow player trust status.");

//-----------------------------------------------------------------------------
// Observer / Tournament
//-----------------------------------------------------------------------------
static ConVar cl_observer_preset_playerHash("cl_observer_preset_playerHash", "", FCVAR_RELEASE, "Observer preset player hash.");
static ConVar cl_observer_preset_playerSlot("cl_observer_preset_playerSlot", "-1", FCVAR_RELEASE, "Observer preset player slot.");
static ConVar cl_observer_preset_team("cl_observer_preset_team", "-1", FCVAR_RELEASE, "Observer preset team.");
static ConVar sv_private_assist_style_override("sv_private_assist_style_override", "-1", FCVAR_RELEASE, "Private match assist style override.");
static ConVar sv_tournament_assist_style_override("sv_tournament_assist_style_override", "-1", FCVAR_RELEASE, "Tournament assist style override.");

//-----------------------------------------------------------------------------
// UI / Settings
//-----------------------------------------------------------------------------
static ConVar autoConnect("autoConnect", "1", FCVAR_RELEASE, "Auto-connect to server.");
static ConVar skipIntroVideos("skipIntroVideos", "0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Skip intro videos.");
static ConVar lobby_battlepass_milestone_enabled("lobby_battlepass_milestone_enabled", "0", FCVAR_RELEASE, "Enable battlepass milestone UI.");
static ConVar hud_setting_anonymousMode("hud_setting_anonymousMode", "0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Anonymous mode for streamer privacy.");
static ConVar net_netGraph2("net_netGraph2", "0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Net graph display mode.");
static ConVar AoCLanguageNeeded("AoCLanguageNeeded", "0", FCVAR_RELEASE, "Age of consent language needed.");
static ConVar NewAoCDownloadComplete("NewAoCDownloadComplete", "0", FCVAR_RELEASE, "New age of consent download complete.");

//-----------------------------------------------------------------------------
// Social / Clubs
//-----------------------------------------------------------------------------
static ConVar Clubs_oldJoinFlow("Clubs_oldJoinFlow", "0", FCVAR_RELEASE, "Use old club join flow.");
static ConVar clubs_showInvites("clubs_showInvites", "0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Show club invites.");
static ConVar CrossPlay_user_optin("CrossPlay_user_optin", "0", FCVAR_RELEASE | FCVAR_ARCHIVE, "User opted into cross-play.");
static ConVar crossPlay_enabled("crossPlay_enabled", "0", FCVAR_RELEASE, "Cross-play enabled.");
static ConVar friends_joinUsePresence("friends_joinUsePresence", "1", FCVAR_RELEASE, "Use presence for friend join.");
static ConVar allow_comms_filter("allow_comms_filter", "0", FCVAR_RELEASE, "Allow communications filter.");
static ConVar cl_comms_filter("cl_comms_filter", "0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Communications filter level.");
static ConVar communicationBlock_warning_fade("communicationBlock_warning_fade", "1.0", FCVAR_RELEASE, "Comm block warning fade time.");
static ConVar communicationBlock_warning_sustain("communicationBlock_warning_sustain", "3.0", FCVAR_RELEASE, "Comm block warning sustain time.");

//-----------------------------------------------------------------------------
// Gameplay
//-----------------------------------------------------------------------------
static ConVar artifacts_tier_override("artifacts_tier_override", "-1", FCVAR_RELEASE, "Override artifacts tier (-1 = no override).");
static ConVar bot_use_loadout_datatables("bot_use_loadout_datatables", "0", FCVAR_RELEASE, "Bots use loadout datatables.");
static ConVar player_setting_enable_heartbeat_sounds("player_setting_enable_heartbeat_sounds", "1", FCVAR_RELEASE | FCVAR_ARCHIVE, "Enable heartbeat sounds when low health.");

//-----------------------------------------------------------------------------
// Season Quest
//-----------------------------------------------------------------------------
static ConVar seasonquest_force_missionscleared_count("seasonquest_force_missionscleared_count", "-1", FCVAR_RELEASE, "Force missions cleared count (-1 = default).");
static ConVar seasonquest_force_pages_read_count("seasonquest_force_pages_read_count", "-1", FCVAR_RELEASE, "Force pages read count (-1 = default).");
static ConVar seasonquest_force_treasurepacks_count("seasonquest_force_treasurepacks_count", "-1", FCVAR_RELEASE, "Force treasure packs count (-1 = default).");

//-----------------------------------------------------------------------------
// Automation / Dev
//-----------------------------------------------------------------------------
static ConVar ui_automation_enabled("ui_automation_enabled", "0", FCVAR_RELEASE, "Enable UI automation.");
static ConVar ui_automation_playlist("ui_automation_playlist", "", FCVAR_RELEASE, "UI automation playlist.");
static ConVar script_R5DEV_220022_Disconnect("script_R5DEV_220022_Disconnect", "0", FCVAR_RELEASE, "Dev disconnect flag.");

//-----------------------------------------------------------------------------
// Gamepad / Input (RESET-only -- scripts call SetConVarToDefault on these)
//-----------------------------------------------------------------------------
static ConVar gamepad_aim_assist_ads_high_power_scopes("gamepad_aim_assist_ads_high_power_scopes", "1.0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Aim assist ADS high power scopes.");
static ConVar gamepad_aim_assist_ads_low_power_scopes("gamepad_aim_assist_ads_low_power_scopes", "1.0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Aim assist ADS low power scopes.");
static ConVar gamepad_aim_assist_hip_high_power_scopes("gamepad_aim_assist_hip_high_power_scopes", "1.0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Aim assist hip high power scopes.");
static ConVar gamepad_aim_assist_hip_low_power_scopes("gamepad_aim_assist_hip_low_power_scopes", "1.0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Aim assist hip low power scopes.");
static ConVar gamepad_aim_assist_melee("gamepad_aim_assist_melee", "1.0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Aim assist melee.");
static ConVar gamepad_custom_assist_style("gamepad_custom_assist_style", "0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Custom assist style.");

//-----------------------------------------------------------------------------
// Laser Sight
//-----------------------------------------------------------------------------
static ConVar laserSightColor("laserSightColor", "255 0 0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Laser sight color (R G B).");
static ConVar laserSightColorCustomized("laserSightColorCustomized", "0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Laser sight color customized.");

//-----------------------------------------------------------------------------
// Audio
//-----------------------------------------------------------------------------
static ConVar miles_channels_menuoption("miles_channels_menuoption", "0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Miles audio channels menu option.");
static ConVar miles_output_device("miles_output_device", "", FCVAR_RELEASE | FCVAR_ARCHIVE, "Miles audio output device name.");
static ConVar voice_input_device("voice_input_device", "", FCVAR_RELEASE | FCVAR_ARCHIVE, "Voice input device name.");

//-----------------------------------------------------------------------------
// Nintendo Switch / Motion (RESET-only, platform-specific stubs)
//-----------------------------------------------------------------------------
static ConVar nx_is_control_spawn_menu_open("nx_is_control_spawn_menu_open", "0", FCVAR_RELEASE, "NX control spawn menu open state.");
static ConVar nx_six_axis_ads_horizontalScale("nx_six_axis_ads_horizontalScale", "1.0", FCVAR_RELEASE | FCVAR_ARCHIVE, "NX gyro ADS horizontal scale.");
static ConVar nx_six_axis_ads_sensitivity("nx_six_axis_ads_sensitivity", "1.0", FCVAR_RELEASE | FCVAR_ARCHIVE, "NX gyro ADS sensitivity.");
static ConVar nx_six_axis_ads_verticalScale("nx_six_axis_ads_verticalScale", "1.0", FCVAR_RELEASE | FCVAR_ARCHIVE, "NX gyro ADS vertical scale.");
static ConVar nx_six_axis_control_on("nx_six_axis_control_on", "0", FCVAR_RELEASE | FCVAR_ARCHIVE, "NX gyro control enabled.");
static ConVar nx_six_axis_horizontalScale("nx_six_axis_horizontalScale", "1.0", FCVAR_RELEASE | FCVAR_ARCHIVE, "NX gyro horizontal scale.");
static ConVar nx_six_axis_sensitivity("nx_six_axis_sensitivity", "1.0", FCVAR_RELEASE | FCVAR_ARCHIVE, "NX gyro sensitivity.");
static ConVar nx_six_axis_verticalScale("nx_six_axis_verticalScale", "1.0", FCVAR_RELEASE | FCVAR_ARCHIVE, "NX gyro vertical scale.");
static ConVar motion_ads_advanced_sensitivity_scalar_0("motion_ads_advanced_sensitivity_scalar_0", "1.0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Motion ADS sensitivity scalar 0.");
static ConVar motion_ads_advanced_sensitivity_scalar_1("motion_ads_advanced_sensitivity_scalar_1", "1.0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Motion ADS sensitivity scalar 1.");
static ConVar motion_ads_advanced_sensitivity_scalar_2("motion_ads_advanced_sensitivity_scalar_2", "1.0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Motion ADS sensitivity scalar 2.");
static ConVar motion_ads_advanced_sensitivity_scalar_3("motion_ads_advanced_sensitivity_scalar_3", "1.0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Motion ADS sensitivity scalar 3.");
static ConVar motion_ads_advanced_sensitivity_scalar_4("motion_ads_advanced_sensitivity_scalar_4", "1.0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Motion ADS sensitivity scalar 4.");
static ConVar motion_ads_advanced_sensitivity_scalar_5("motion_ads_advanced_sensitivity_scalar_5", "1.0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Motion ADS sensitivity scalar 5.");
static ConVar motion_ads_advanced_sensitivity_scalar_6("motion_ads_advanced_sensitivity_scalar_6", "1.0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Motion ADS sensitivity scalar 6.");
static ConVar motion_ads_advanced_sensitivity_scalar_7("motion_ads_advanced_sensitivity_scalar_7", "1.0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Motion ADS sensitivity scalar 7.");
static ConVar motion_use_per_scope_sensitivity_scalars("motion_use_per_scope_sensitivity_scalars", "0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Use per-scope motion sensitivity scalars.");

