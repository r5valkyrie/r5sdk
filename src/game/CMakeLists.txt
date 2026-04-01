cmake_minimum_required( VERSION 3.16 )

macro( add_game_project PROJECT_NAME )
add_module( "lib" ${PROJECT_NAME} "vpc" ${FOLDER_CONTEXT} TRUE TRUE )

start_sources()

if( ${PROJECT_NAME} STREQUAL "game_shared_static" )

add_sources( SOURCE_GROUP "AI"
    "shared/ai_utility_shared.cpp"
    "shared/ai_utility_shared.h"
    "shared/navmesh_debug_draw.cpp"
    "shared/navmesh_debug_draw.h"
)

add_sources( SOURCE_GROUP "Entity"
    "shared/ehandle.h"
)

add_sources( SOURCE_GROUP "Simulation"
    "shared/predictioncopy.h"
    "shared/simtimer.cpp"
    "shared/simtimer.h"
)

add_sources( SOURCE_GROUP "Animation"
    "shared/animation.cpp"
    "shared/animation.h"
)

add_sources( SOURCE_GROUP "Activity"
    "shared/activity.cpp"
    "shared/activity.h"
    "shared/activitymodifier.cpp"
    "shared/activitymodifier.h"
)

add_sources( SOURCE_GROUP "Effects"
    "shared/status_effect.h"
    "shared/status_effects_sdk.cpp"
    "shared/status_effects_sdk.h"
    "shared/particleproperty.h"
    "shared/particle_parse.h"
)

add_sources( SOURCE_GROUP "Physics"
    "shared/collisionproperty.cpp"
    "shared/collisionproperty.h"
)

add_sources( SOURCE_GROUP "Player"
    "shared/player_viewoffset.h"
    "shared/player_melee.h"
)

add_sources( SOURCE_GROUP "Weapon"
    "shared/r1/weapon_playerdata.h"
    "shared/r1/weapon_parse.h"
    "shared/r1/blast_pattern.cpp"
    "shared/r1/blast_pattern.h"
    "shared/r1/weapon_bolt.cpp"
    "shared/r1/weapon_bolt.h"
    "shared/r1/grapple.h"
    "shared/r1/smartammo.h"
    "shared/weapon_script_vars.cpp"
    "shared/weapon_script_vars.h"
    "shared/weapon_heat.cpp"
    "shared/weapon_heat.h"
    "shared/globalnonrewind_vars.cpp"
    "shared/globalnonrewind_vars.h"
    "shared/deathfield_system.cpp"
    "shared/deathfield_system.h"
    "shared/highlight_context.cpp"
    "shared/highlight_context.h"
    "shared/dt_injection.cpp"
    "shared/dt_injection.h"
)

add_sources( SOURCE_GROUP "Network"
    "shared/playernet_vars.h"
    "shared/usermessages.h"
)

add_sources( SOURCE_GROUP "Input"
    "shared/usercmd.cpp"
    "shared/usercmd.h"
    "shared/pingcmd.h"
)

add_sources( SOURCE_GROUP "Utility"
    "shared/imovehelper.h"
    "shared/util_shared.cpp"
    "shared/util_shared.h"
)

add_sources( SOURCE_GROUP "Shared"
    "shared/shared_classnames.h"
    "shared/shareddefs.h"
    "shared/takedamageinfo.h"
    "shared/shared_activity.h"
)

add_sources( SOURCE_GROUP "Script"
    "shared/vscript_shared.cpp"
    "shared/vscript_shared.h"
    "shared/vscript_debug_overlay_shared.cpp"
    "shared/vscript_debug_overlay_shared.h"
    "shared/vscript_gamedll_defs.h"
    "shared/scriptremotefunctions.cpp"
    "shared/scriptremotefunctions.h"
    "shared/scriptremotefunctions_shared.cpp"
    "shared/scriptremotefunctions_shared.h"
)

add_sources( SOURCE_GROUP "ModSystem"
    "shared/pluginsystem/modsystem.cpp"
    "shared/pluginsystem/modsystem.h"
)

endif()

if( ${PROJECT_NAME} STREQUAL "server_static" )

add_sources( SOURCE_GROUP "AI"
    "server/ai_basenpc.cpp"
    "server/ai_basenpc.h"
    "server/ai_network.cpp"
    "server/ai_network.h"
    "server/ai_networkmanager.cpp"
    "server/ai_networkmanager.h"
    "server/ai_commands.cpp"
    "server/ai_node.h"
    "server/ai_utility.cpp"
    "server/ai_utility.h"
    "server/ai_task.cpp"
    "server/ai_task.h"
    "server/ai_schedule.cpp"
    "server/ai_schedule.h"
    "server/ai_component.h"
    "server/ai_senses.h"
    "server/ai_localnavigator.h"
    "server/ai_navigator.h"
    "server/ai_navtypes.h"
    "server/ai_movetypes.h"
    "server/ai_moveprobe.h"
    "server/ai_motor.h"
    "server/ai_route.h"
    "server/ai_interest.cpp"
    "server/ai_interest.h"
    "server/ai_waypoint.h"
    "server/ai_pathfinder.cpp"
    "server/ai_pathfinder.h"
    "server/ai_memory.h"
    "server/ai_squad.h"
    "server/ai_tacticalservices.h"
    "server/ai_moveshoot.h"
    "server/ai_battlelocation.h"
    "server/ai_syncedmelee.h"
    "server/ai_shootcover.h"
    "server/ai_hint.h"
    "server/ai_npcstate.h"
    "server/detour_impl.h"
)

add_sources( SOURCE_GROUP "Entity"
    "server/baseanimating.cpp"
    "server/baseanimating.h"
    "server/baseanimatingoverlay.h"
    "server/basecombatcharacter.h"
    "server/baseentity.cpp"
    "server/baseentity.h"
    "server/entitylist.cpp"
    "server/entitylist.h"
    "server/entityoutput.cpp"
    "server/entityoutput.h"
    "server/soundent.cpp"
    "server/soundent.h"
    "server/entitylist_serverbase.h"
)

add_sources( SOURCE_GROUP "Network"
    "server/networkproperty.cpp"
    "server/networkproperty.h"
)

add_sources( SOURCE_GROUP "Player"
    "server/player.cpp"
    "server/player.h"
    "server/player_command.cpp"
    "server/player_command.h"
    "server/playerlocaldata.h"
)

add_sources( SOURCE_GROUP "Weapon"
    "server/r1/weapon_x.cpp"
    "server/r1/weapon_x.h"
)

add_sources( SOURCE_GROUP "Script"
    "server/vscript_server.cpp"
    "server/vscript_server.h"
    "shared/scriptremotefunctions_server.cpp"
    "shared/scriptremotefunctions_server.h"
)

add_sources( SOURCE_GROUP "Physics"
    "server/physics_main.cpp"
    "server/physics_main.h"
)

add_sources( SOURCE_GROUP "Utility"
    "server/cbase.cpp"
    "server/cbase.h"
    "server/gameinterface.cpp"
    "server/gameinterface.h"
    "server/movehelper_server.cpp"
    "server/movehelper_server.h"
    "server/util_server.cpp"
    "server/util_server.h"
    "server/variant_t.cpp"
    "server/variant_t.h"
)

add_sources( SOURCE_GROUP "LiveAPI"
    "server/liveapi/liveapi.cpp"
    "server/liveapi/liveapi.h"
)

add_sources( SOURCE_GROUP "Recipient Filter"
    "server/recipientfilter.cpp"
    "server/recipientfilter.h"
)

file( GLOB GAME_SERVER_PUBLIC_HEADERS
    "${ENGINE_SOURCE_DIR}/public/game/server/*"
)

endif()

if( ${PROJECT_NAME} STREQUAL "client_static" )

add_sources( SOURCE_GROUP "Entity"
    "client/c_baseentity.cpp"
    "client/c_baseentity.h"
    "client/c_baseanimating.h"
    "client/c_baseanimatingoverlay.h"
    "client/c_basecombatcharacter.h"
    "client/cliententitylist.cpp"
    "client/cliententitylist.h"
    "client/entitylist_clientbase.h"
    "client/icliententityinternal.h"
)

add_sources( SOURCE_GROUP "Player"
    "client/c_player.cpp"
    "client/c_player.h"
    "client/c_playerlocaldata.h"
)

add_sources( SOURCE_GROUP "Weapon"
    "client/r1/c_weapon_x.cpp"
    "client/r1/c_weapon_x.h"
)

add_sources( SOURCE_GROUP "Input"
    "client/in_main.cpp"
    "client/input.cpp"
    "client/input.h"
    "client/gamepad.cpp"
    "client/gamepad.h"
    "client/kbutton.h"
)

add_sources( SOURCE_GROUP "Leaf System"
    "client/clientleafsystem.cpp"
    "client/clientleafsystem.h"
)

add_sources( SOURCE_GROUP "Rendering"
    "client/enginesprite.h"
    "client/hud.h"
    "client/spritemodel.cpp"
    "client/viewrender.cpp"
    "client/viewrender.h"
    "client/iviewrender.h"
)

add_sources( SOURCE_GROUP "Animation"
    "client/animationlayer.h"
)

add_sources( SOURCE_GROUP "Utility"
    "client/movehelper_client.cpp"
    "client/movehelper_client.h"
    "client/util_client.cpp"
    "client/util_client.h"
)

add_sources( SOURCE_GROUP "Script"
    "client/vscript_client.cpp"
    "client/vscript_client.h"
    "client/vscript_colorpalette.cpp"
    "client/vscript_colorpalette.h"
    "client/vscript_hud.cpp"
    "client/vscript_hud.h"
    "client/vscript_player.cpp"
    "client/vscript_player.h"
    "client/vscript_remotefunctions.cpp"
    "client/vscript_remotefunctions.h"
    "client/concommandcallback.cpp"
    "client/concommandcallback.h"
    "client/convar_stubs.cpp"
    "client/scriptnetdata_client.cpp"
    "client/scriptnetdata_client.h"
    "client/particle_effects_sdk.cpp"
    "client/particle_effects_sdk.h"
)

add_sources( SOURCE_GROUP "Viewmodel"
    "client/viewmodel_poseparam.cpp"
    "client/viewmodel_poseparam.h"
)

add_sources( SOURCE_GROUP "Hud"
    "client/hud_basechat.cpp"
    "client/hud_basechat.h"
)

add_sources( SOURCE_GROUP "Zipline"
    "client/c_zipline_fix.cpp"
    "client/c_zipline_fix.h"
)

add_sources( SOURCE_GROUP "RUI"
    "client/ruitracks.cpp"
    "client/ruitracks.h"
    "client/rui_walltime.cpp"
    "client/rui_walltime.h"
)

file( GLOB GAME_CLIENT_PUBLIC_HEADERS
    "${ENGINE_SOURCE_DIR}/public/game/client/*"
)

endif()

file( GLOB GAME_SHARED_PUBLIC_HEADERS
    "${ENGINE_SOURCE_DIR}/public/game/shared/*"
)

end_sources()

if( ${PROJECT_NAME} STREQUAL "server_static" )

target_compile_definitions( ${PROJECT_NAME} PRIVATE
    "SERVER_DLL"
)
elseif( ${PROJECT_NAME} STREQUAL "client_static" )
target_compile_definitions( ${PROJECT_NAME} PRIVATE
    "CLIENT_DLL"
)

endif()

target_include_directories( ${PROJECT_NAME} PRIVATE
    "${ENGINE_SOURCE_DIR}/tier0/"
    "${ENGINE_SOURCE_DIR}/tier1/"
    "${THIRDPARTY_SOURCE_DIR}/mbedtls/include"
)

endmacro()

add_game_project( "server_static" )
add_game_project( "client_static" )
add_game_project( "game_shared_static" )
