cmake_minimum_required( VERSION 3.16 )

macro( add_sdk_project PROJECT_NAME )
add_module( "shared_lib" ${PROJECT_NAME} "vpc" ${FOLDER_CONTEXT} TRUE TRUE )

start_sources()

if( NOT ${PROJECT_NAME} STREQUAL "dedicated" )
file( GLOB PNG_SOURCES
    "${ENGINE_SOURCE_DIR}/resource/png/*.png"
)
add_sources( SOURCE_GROUP "Resource"
    "${ENGINE_SOURCE_DIR}/resource/r5dev.rc"
    "${PNG_SOURCES}"
)

# Add Steam integration for non-dedicated builds
add_sources( SOURCE_GROUP "Steam"
    "${ENGINE_SOURCE_DIR}/engine/client/steam_integration.cpp"
    "${ENGINE_SOURCE_DIR}/engine/client/steam_integration.h"
    "${ENGINE_SOURCE_DIR}/engine/client/steam_api_wrapper.cpp"
)
endif()

add_sources( SOURCE_GROUP "Core"
    "assert.h"
    "dllmain.cpp"
    "init.cpp"
    "init.h"
    "logdef.cpp"
    "logdef.h"
    "logger.cpp"
    "logger.h"
    "r5dev.h"
    "resource.h"
    "shared_pch.h"
    "stdafx.cpp"
    "stdafx.h"
    "termutil.cpp"
    "termutil.h"
)

target_link_libraries( ${PROJECT_NAME} PRIVATE
    "advapi32.lib"
    "bcrypt.lib"
    "crypt32.lib"
    "dbghelp.lib"
    "wldap32.lib"
    "ws2_32.lib"
    "Rpcrt4.lib"
    "iphlpapi.lib"
    "Winmm.lib"

    "boost_charconv"
    "boost_regex"

    "vpc"
    "memoverride"
    "tier0"
    "tier1"
    "tier2"
    "launcher"
    "appframework"

    "vstdlib"
    "vpklib"
    "mathlib"
    "vphysics"

    "SigCache_Pb"
    "LiveAPI_Pb"
    "NetCon_Pb"

    "rson"
    "rtech_game"
    "playlists"
    "stryder"

    "liblzham"
    "libzstd"
    "liblz4"

    "libdetours"
    "libcurl"
    "libprotobuf"
    "libspdlog"
    "libdetour"
    "navdebugutils"
    "navsharedcommon"

    "EAThread"
    "DirtySDK"

    "libmbedcrypto"
    "libmbedtls"
    "libmbedx509"

    "networksystem"
    "pluginsystem"
    "filesystem"
    "datacache"
    "EbisuSDK"
    "GFSDK"
    "RDSDK"

    "localize"

    "vscript"
    "game_shared_static"
)

if( NOT ${PROJECT_NAME} STREQUAL "dedicated" )
target_link_libraries( ${PROJECT_NAME} PRIVATE
    "libimgui"

    "milessdk"
    "codecs"

    "inputsystem"
    "materialsystem"

    "studiorender"

    "vguimatsurface"
    "vgui"
    "rui"
    "datatable"

    "particles"

    "d3d11.lib"
    "${THIRDPARTY_SOURCE_DIR}/nvapi/amd64/nvapi64.lib"
)
endif()

if( NOT ${PROJECT_NAME} STREQUAL "client" )
target_link_libraries( ${PROJECT_NAME} PRIVATE
    "libjwt"
)
endif()

# Determine the compiler definitions and link libraries per project.
if( ${PROJECT_NAME} STREQUAL "gamesdk" )
end_sources()

target_link_libraries( ${PROJECT_NAME} PRIVATE
    "engine"
    "server_static"
    "client_static"
)
target_compile_definitions( ${PROJECT_NAME} PRIVATE
    "GAMESDK"
)

elseif( ${PROJECT_NAME} STREQUAL "dedicated" )
end_sources()

target_link_libraries( ${PROJECT_NAME} PRIVATE
    "materialsystem_nodx" # Needs the No-DirectX version for patching.
    "engine_ds"
    "server_static"
)
target_compile_definitions( ${PROJECT_NAME} PRIVATE
    "MATERIALSYSTEM_NODX"
    "SERVER_DLL"
    "DEDICATED"
)

elseif( ${PROJECT_NAME} STREQUAL "client" )
end_sources( "${BUILD_OUTPUT_DIR}/bin/x64_retail/" )

target_link_libraries( ${PROJECT_NAME} PRIVATE
    "engine_cl"
    "client_static"
)
target_compile_definitions( ${PROJECT_NAME} PRIVATE
    "CLIENT_DLL"
)

endif()

target_include_directories( ${PROJECT_NAME} PRIVATE
	"${THIRDPARTY_SOURCE_DIR}/dirtysdk/include/"
	"${THIRDPARTY_SOURCE_DIR}/ea/"
)

target_link_options( ${PROJECT_NAME} PRIVATE
    "/STACK:8000000" # Match game executable stack reserve size

    # The only reason this has to be done, is because
    # the _xxxx_dbg malloc functions could otherwise
    # throw a linker error, as they are already defined
    # in the CRT library, but there doesn't seem to be
    # a standard way to override these, unlike the
    # _xxxx_base malloc functions. Since this is only
    # relevant for debug builds, this linker flag should
    # only be toggled for DEBUG!!!
    $<$<CONFIG:Debug>:/FORCE:MULTIPLE>
)

add_custom_command( TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -P ${ENGINE_SOURCE_DIR}/cmake/PostBuild.cmake
)

if( ${PROJECT_NAME} STREQUAL "dedicated" )
set_target_properties( ${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_COMMAND "r5apex_ds.exe"
)
else()
set_target_properties( ${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_COMMAND "r5apex.exe"
)
endif()

if( ${PROJECT_NAME} STREQUAL "client" )
set_target_properties( ${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_COMMAND_ARGUMENTS "-wconsole -ansicolor -dev -devsdk -noserverdll"
)
else()
set_target_properties( ${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_COMMAND_ARGUMENTS "-wconsole -ansicolor -dev -devsdk"
    VS_DEBUGGER_WORKING_DIRECTORY "$(ProjectDir)../../../${BUILD_OUTPUT_DIR}/"
)
endif()

set_target_properties( ${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$(ProjectDir)../../../${BUILD_OUTPUT_DIR}/"
)

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
    if( NOT ${PROJECT_NAME} STREQUAL "dedicated" )
        # Add USE_STEAMWORKS define to all files in the target
        target_compile_definitions(${PROJECT_NAME} PRIVATE USE_STEAMWORKS)
        
        # Exclude steam_api_wrapper.cpp from precompiled headers to avoid type conflicts
        # between r5sdk's sdkint.h and Steam's steamtypes.h
        set_source_files_properties(
            "${ENGINE_SOURCE_DIR}/engine/client/steam_api_wrapper.cpp"
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
                target_link_libraries( ${PROJECT_NAME} PRIVATE steamworks_api )
            else()
                message(WARNING "Steamworks library not found at: ${_STEAM_IMP}")
            endif()
        endif()
    endif()
endif()

endmacro()

add_sdk_project( "gamesdk" )
add_sdk_project( "dedicated" )
add_sdk_project( "client" )
