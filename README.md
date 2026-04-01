# Source SDK
* This repository houses the source code for the development package targeting the game **Apex Legends**.

## Building
R5sdk uses the CMake project generation and build tools. For more information, visit [CMake](https://cmake.org/).<br />
In order to compile the SDK, you will need to install Visual Studio 2026 with:
* Desktop Development with C++ Package.
* Windows SDK 10.0.10240.0 or higher.
* C++ MFC build tools for x86 and x64.
* [Optional] C++ Clang/LLVM compiler.

Steps:
1. Download or clone the project to anywhere on your disk.
2. Open CMAKE GUI and select the project folder as source code.
3. Press Configure and select VS 2026
4. Once configuration is done, change these settings
  - BOOST_REGEX_STANDALONE - Disabled
  - OPTION_CERTAIN - Enabled
  - OPTION_LTCG_MODE - All
  - OPTION_RETAIL - Enabled
  - OPTION_WARNINGS_AS_ERRORS - Disabled
5. Open `r5sdk.slnx` in Visual Studio and compile the solution in retail x64 mode.
  - All binaries and symbols are compiled to the `game` folder. Copy them to R5Valkyrie dir.
  - Run `r5apex.exe`, or launch the game via R5Valkyrie launcher.

## Steamworks Integration [OPTIONAL]
The SDK includes optional Steam integration for features like authentication, user profiles, and overlay support. **This is completely optional** - the SDK works without Steam.

### Setting up Steamworks (Optional)
Due to licensing restrictions, the Steamworks SDK is not included in this repository. If you want Steam features:

1. **Download the Steamworks SDK:**
   - Visit [Steamworks SDK](https://partner.steamgames.com/) (requires Steam partner account)
   - Download the latest Steamworks SDK

2. **Install the SDK:**
   - Extract the SDK to `src/thirdparty/steamworks/sdk/`
   - The folder structure should look like:
     ```
     src/thirdparty/steamworks/sdk/
     ├── public/steam/
     ├── redistributable_bin/
     └── Readme.txt
     ```

3. **Enable Steam features:**
   - Add `#define USE_STEAMWORKS` to your project or compiler flags
   - The build system will automatically link against Steam libraries when available

### Steam Features
When Steamworks is enabled, the SDK provides:
- **Steam Authentication**: Session tickets for server authentication
- **User Profiles**: Access to Steam username and user ID
- **Steam Overlay**: Integration with Steam's in-game overlay
- **Safe Integration**: Automatic fallbacks when Steam is unavailable

### Console Commands (with Steam)
- `steam_overlay_info` - Display Steam overlay status and settings
- `steam_overlay_pos <0-3>` - Set overlay notification position
- `steam_safe_callbacks <0/1>` - Enable/disable safe callback processing

### Building without Steam
The SDK compiles and runs perfectly without Steamworks. All Steam-related code is conditionally compiled and safely disabled when the SDK is not available.

## Debugging
The tools and libraries offered by the SDK could be debugged right after they are compiled.

Steps:
1. Set the target project as **Startup Project**.
    1. Select `Project -> Set as Startup Project`.
2. Configure the project's debugging settings.
    1. Debug settings are found in `Project -> Properties -> Configuration Properties -> Debugging`.
    2. Additional command line arguments could be set in the `Command Arguments` field.

## Launch Parameters
- The `-wconsole` parameter toggles the external console window to which output of the game is getting logged to.
- The `-ansicolor` parameter enables colored console output to enhance readability (NOTE: unsupported for some OS versions!).
- The `-nosmap` parameter instructs the SDK to always compute the RVA's of each function signature on launch (!! slow !!).
- The `-noworkerdll` parameter prevents the GameSDK DLL from initializing (workaround as the DLL is imported by the game executable).

Launch parameters can be added to the `startup_*.cfg` files,<br />
which are located in `<gamedir>\platform\cfg\startup_*.cfg`.

## Note [IMPORTANT]
This is not a cheat or hack; attempting to use the SDK on the live version of the game could result in a permanent account ban. The supported game versions are:

 * S3 `R5pc_r5launch_N1094_CL456479_2019_10_30_05_20_PM`.

## Pylon [DISCLAIMER]
When you host game servers on the Server Browser (Pylon) you will stream your IP address to the database,
which will be stored there until you stop hosting the server; this is needed so other people can connect to your server.

There is a checkbox in the Server Browser called `Server Visibility` that defaults to `Offline`.
- `Offline`: No data is broadcasted to the master server; you are playing offline.
- `Hidden`: Your server will be broadcasted to the master server, but could only be joined using a private token.
- `Online`: Your server will be broadcasted to the master server, and could be joined from the public list.

Alternatively, you can host game servers without the use of our master server. You can grant people access to your game server
by sharing the IP address and port manually. The client can connect using the `connect` command. The usage of the `connect`
command is as follows: IPv4 `connect 127.0.0.1:37015`, IPv6 `connect [::1]:37015`. NOTE: the IP address and port were examples.
