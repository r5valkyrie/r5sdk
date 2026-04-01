cmake_minimum_required( VERSION 3.16 )

macro( add_engine_project PROJECT_NAME )
add_module( "lib" ${PROJECT_NAME} "vpc" ${FOLDER_CONTEXT} TRUE TRUE )

start_sources()

add_sources( SOURCE_GROUP "Engine"
    "clockdriftmgr.cpp"
    "clockdriftmgr.h"
    "cmd.cpp"
    "cmd.h"
    "common.cpp"
    "common.h"
)

add_sources( SOURCE_GROUP "Collision"
    "cmodel_bsp_debug.cpp"
    "cmodel_bsp_debug.h"
    "enginetrace.cpp"
    "enginetrace.h"
    "traceinit.h"
)

add_sources( SOURCE_GROUP "Debug"
    "debugoverlay.cpp"
    "debugoverlay.h"
    "vis_debug.cpp"
    "vis_debug.h"
)

if( NOT ${PROJECT_NAME} STREQUAL "engine_ds" )
add_sources( SOURCE_GROUP "Input"
    "keys.cpp"
    "keys.h"
)

add_sources( SOURCE_GROUP "Render"
    "framelimit.cpp"
    "framelimit.h"
    "gl_drawlights.cpp"
    "gl_drawlights.h"
    "gl_matsysiface.h"
    "gl_model_private.h"
    "gl_rmain.cpp"
    "gl_rmain.h"
    "gl_rsurf.cpp"
    "gl_rsurf.h"
    "gl_screen.cpp"
    "gl_screen.h"
    "matsys_interface.cpp"
    "matsys_interface.h"
)
endif()

add_sources( SOURCE_GROUP "Network"
    "dt_recv.cpp"
    "datamap.cpp"
    "framesnapshot.h"
    "net.cpp"
    "net.h"
    "net_chan.cpp"
    "net_chan.h"
    "networkstringtable.cpp"
    "networkstringtable.h"
    "packed_entity.h"
    "staticpropmgr.cpp"
    "staticpropmgr.h"
    "staticprop_bounds_debug.cpp"
    "staticprop_bounds_debug.h"
    # "networkvar.cpp" # TODO: Enable?
)

add_sources( SOURCE_GROUP "Model"
    "cmodel.cpp"
    "cmodel.h"
    "cmodel_bsp.cpp"
    "cmodel_bsp.h"
    "modelinfo.cpp"
    "modelinfo.h"
    "modelloader.cpp"
    "modelloader.h"
    "sv_model_precache_ext.cpp"
    "sv_model_precache_ext.h"
)

add_sources( SOURCE_GROUP "Host"
    "host.cpp"
    "host.h"
    "host_cmd.cpp"
    "host_cmd.h"
    "host_state.cpp"
    "host_state.h"
)

add_sources( SOURCE_GROUP "System"
    "sys_dll.cpp"
    "sys_dll.h"
    "sys_dll2.cpp"
    "sys_dll2.h"
    "sys_engine.cpp"
    "sys_engine.h"
    "sys_utils.cpp"
    "sys_utils.h"
    "sdk_dll.cpp"
    "sdk_dll.h"
)

if( NOT ${PROJECT_NAME} STREQUAL "engine_ds" )
add_sources( SOURCE_GROUP "System"
    "sys_getmodes.cpp"
    "sys_getmodes.h"
    "sys_mainwind.cpp"
    "sys_mainwind.h"
)
endif()

if( NOT ${PROJECT_NAME} STREQUAL "engine_cl" )
add_sources( SOURCE_GROUP "Server"
    "server/persistence.cpp"
    "server/persistence.h"
    "server/server.cpp"
    "server/server.h"
    "server/sv_rcon.cpp"
    "server/sv_rcon.h"
    "server/sv_main.cpp"
    "server/sv_main.h"
    "server/vengineserver_impl.cpp"
    "server/vengineserver_impl.h"
    "server/datablock_sender.cpp"
    "server/datablock_sender.h"
)
endif()

add_sources( SOURCE_GROUP "Client"
    "client/cdll_engine_int.cpp"
    "client/cdll_engine_int.h"
    "client/cl_ents_parse.cpp"
    "client/cl_ents_parse.h"
    "client/client.cpp"
    "client/client.h"
)

add_sources( SOURCE_GROUP "Shared"
    "shared/base_rcon.cpp"
    "shared/base_rcon.h"
    "shared/shared_rcon.cpp"
    "shared/shared_rcon.h"
    "shared/datablock.cpp"
    "shared/datablock.h"
    "shared/ents_shared.h"
)

if( NOT ${PROJECT_NAME} STREQUAL "engine_ds" )
add_sources( SOURCE_GROUP "Client" # Client only.
    "client/clientstate.cpp"
    "client/clientstate.h"
    "client/discord_ipc.cpp"
    "client/discord_ipc.h"
    "client/discord_presence.cpp"
    "client/discord_presence.h"
    "client/discord_rpc_wrapper.cpp"
    "client/discord_rpc_wrapper.h"
    "client/cl_main.cpp"
    "client/cl_main.h"
    "client/cl_splitscreen.cpp"
    "client/cl_splitscreen.h"
    "client/cl_rcon.cpp"
    "client/cl_rcon.h"
    "client/vengineclient_impl.cpp"
    "client/vengineclient_impl.h"
    "client/datablock_receiver.cpp"
    "client/datablock_receiver.h"
    "client/community_party.cpp"
    "client/community_party.h"
    "client/steam_integration.cpp"
    "client/steam_integration.h"
    "client/steam_api_wrapper.cpp"
)

add_sources( SOURCE_GROUP "GameUI"
    "${ENGINE_SOURCE_DIR}/gameui/IBrowser.cpp"
    "${ENGINE_SOURCE_DIR}/gameui/IBrowser.h"
    "${ENGINE_SOURCE_DIR}/gameui/IConsole.cpp"
    "${ENGINE_SOURCE_DIR}/gameui/IConsole.h"
    "${ENGINE_SOURCE_DIR}/gameui/IParticleOverlay.cpp"
    "${ENGINE_SOURCE_DIR}/gameui/IParticleOverlay.h"
    "${ENGINE_SOURCE_DIR}/gameui/IStreamOverlay.cpp"
    "${ENGINE_SOURCE_DIR}/gameui/IStreamOverlay.h"
	"${ENGINE_SOURCE_DIR}/gameui/ITopBar.cpp"
	"${ENGINE_SOURCE_DIR}/gameui/ITopBar.h"
	"${ENGINE_SOURCE_DIR}/gameui/IDevMenu.cpp"
	"${ENGINE_SOURCE_DIR}/gameui/IDevMenu.h"

    "${ENGINE_SOURCE_DIR}/gameui/imgui_system.cpp"
    "${ENGINE_SOURCE_DIR}/gameui/imgui_system.h"
    "${ENGINE_SOURCE_DIR}/gameui/imgui_surface.cpp"
    "${ENGINE_SOURCE_DIR}/gameui/imgui_surface.h"
)
endif()

add_sources( SOURCE_GROUP "Windows"
    "${ENGINE_SOURCE_DIR}/windows/console.cpp"
    "${ENGINE_SOURCE_DIR}/windows/console.h"
    "${ENGINE_SOURCE_DIR}/windows/id3dx.cpp"
    "${ENGINE_SOURCE_DIR}/windows/id3dx.h"
    "${ENGINE_SOURCE_DIR}/windows/input.cpp"
    "${ENGINE_SOURCE_DIR}/windows/input.h"
    "${ENGINE_SOURCE_DIR}/windows/resource.cpp"
    "${ENGINE_SOURCE_DIR}/windows/resource.h"
    "${ENGINE_SOURCE_DIR}/windows/system.cpp"
    "${ENGINE_SOURCE_DIR}/windows/system.h"
)

add_sources( SOURCE_GROUP "Common"
    "${ENGINE_SOURCE_DIR}/common/callback.cpp"
    "${ENGINE_SOURCE_DIR}/common/callback.h"
    "${ENGINE_SOURCE_DIR}/common/completion.cpp"
    "${ENGINE_SOURCE_DIR}/common/completion.h"
    "${ENGINE_SOURCE_DIR}/common/engine_launcher_api.h"
    "${ENGINE_SOURCE_DIR}/common/experimental.h"
    "${ENGINE_SOURCE_DIR}/common/global.cpp"
    "${ENGINE_SOURCE_DIR}/common/global.h"
    "${ENGINE_SOURCE_DIR}/common/igameserverdata.h"
    "${ENGINE_SOURCE_DIR}/common/netmessages.cpp"
    "${ENGINE_SOURCE_DIR}/common/netmessages.h"
    "${ENGINE_SOURCE_DIR}/common/opcodes.cpp"
    "${ENGINE_SOURCE_DIR}/common/opcodes.h"
    "${ENGINE_SOURCE_DIR}/common/protocol.h"
    "${ENGINE_SOURCE_DIR}/common/pseudodefs.h"
    "${ENGINE_SOURCE_DIR}/common/qlimits.h"
    "${ENGINE_SOURCE_DIR}/common/sdkint.h"
    "${ENGINE_SOURCE_DIR}/common/sdkdefs.h"
    "${ENGINE_SOURCE_DIR}/common/x86defs.h"
    "${ENGINE_SOURCE_DIR}/common/xbox/xboxstubs.h"
)

file( GLOB ENGINE_PUBLIC_HEADERS
    "${ENGINE_SOURCE_DIR}/public/engine/*"
)

if( NOT ${PROJECT_NAME} STREQUAL "engine_ds" )
add_sources( SOURCE_GROUP "Public"
    "${ENGINE_SOURCE_DIR}/public/client_class.h"
    "${ENGINE_SOURCE_DIR}/public/ivrenderview.h"
)
endif()

end_sources()

if( ${PROJECT_NAME} STREQUAL "engine_ds" )
target_compile_definitions( ${PROJECT_NAME} PRIVATE
    "DEDICATED"
)
elseif( ${PROJECT_NAME} STREQUAL "engine_cl" )
target_compile_definitions( ${PROJECT_NAME} PRIVATE
    "CLIENT_DLL"
)
endif()

# Auto-detect Steamworks support (checks for SDK availability)
if( DEFINED ENV{STEAM_SDK_DIR} )
    set(STEAM_SDK_DIR $ENV{STEAM_SDK_DIR})
endif()

if( NOT STEAM_SDK_DIR AND EXISTS "${THIRDPARTY_SOURCE_DIR}/steamworks/sdk" )
    set(STEAM_SDK_DIR "${THIRDPARTY_SOURCE_DIR}/steamworks/sdk")
endif()

# Auto-enable Steamworks if SDK is found and required files exist
set(USE_STEAMWORKS OFF)
if( STEAM_SDK_DIR AND EXISTS "${STEAM_SDK_DIR}/public/steam/steam_api.h" )
    if( WIN32 )
        if( CMAKE_SIZEOF_VOID_P EQUAL 8 )
            set(_STEAM_LIB_CHECK "${STEAM_SDK_DIR}/redistributable_bin/win64/steam_api64.lib")
        else()
            set(_STEAM_LIB_CHECK "${STEAM_SDK_DIR}/redistributable_bin/steam_api.lib")
        endif()
        if( EXISTS "${_STEAM_LIB_CHECK}" )
            set(USE_STEAMWORKS ON)
        endif()
    else()
        # For non-Windows platforms, just check for header existence
        set(USE_STEAMWORKS ON)
    endif()
endif()

if( USE_STEAMWORKS AND STEAM_SDK_DIR )
    message(STATUS "Auto-detected Steamworks SDK - Building with Steamworks from: ${STEAM_SDK_DIR}")
else()
    message(STATUS "Steamworks SDK not found - Building without Steamworks support")
endif()

if( USE_STEAMWORKS AND STEAM_SDK_DIR )
    if( NOT ${PROJECT_NAME} STREQUAL "engine_ds" )
        # Add USE_STEAMWORKS define to all files in the target
        target_compile_definitions(${PROJECT_NAME} PRIVATE USE_STEAMWORKS)
        
        # Exclude steam_api_wrapper.cpp from precompiled headers to avoid type conflicts
        # between r5sdk's sdkint.h and Steam's steamtypes.h
        set_source_files_properties(
            "client/steam_api_wrapper.cpp"
            PROPERTIES
            SKIP_PRECOMPILE_HEADERS ON
            INCLUDE_DIRECTORIES "${STEAM_SDK_DIR}/public;${STEAM_SDK_DIR}/public/steam"
        )
        if( WIN32 )
            if( CMAKE_SIZEOF_VOID_P EQUAL 8 )
                set(_STEAM_IMP "${STEAM_SDK_DIR}/redistributable_bin/win64/steam_api64.lib")
            else()
                set(_STEAM_IMP "${STEAM_SDK_DIR}/redistributable_bin/steam_api.lib")
            endif()
            if( EXISTS "${_STEAM_IMP}" )
                if( NOT TARGET steamworks_api )
                    add_library(steamworks_api SHARED IMPORTED GLOBAL)
                    set_target_properties(steamworks_api PROPERTIES IMPORTED_IMPLIB "${_STEAM_IMP}")
                endif()
                # PUBLIC so dependents (e.g., gamesdk) pick up the absolute path to the import lib
                target_link_libraries( ${PROJECT_NAME} PUBLIC steamworks_api )
            else()
                message(WARNING "Steamworks library not found at: ${_STEAM_IMP}")
            endif()
        endif()
    endif()
endif()

target_include_directories( ${PROJECT_NAME} PRIVATE
    "${THIRDPARTY_SOURCE_DIR}/recast/"
    "${THIRDPARTY_SOURCE_DIR}/mbedtls/include"
)

# Discord Rich Presence is built-in using native IPC (no external DLL required)

endmacro()

add_engine_project( "engine" )
add_engine_project( "engine_ds" )
add_engine_project( "engine_cl" )
