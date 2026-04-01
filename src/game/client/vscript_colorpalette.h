#ifndef VSCRIPT_COLORPALETTE_H
#define VSCRIPT_COLORPALETTE_H

class Vector3D;

//-----------------------------------------------------------------------------
// Script registration (called from vscript_client.cpp)
//-----------------------------------------------------------------------------
void Script_RegisterColorPaletteFunctions(CSquirrelVM* s);
void Script_RegisterColorPaletteUIFunctions(CSquirrelVM* s);
void ColorPalette_LevelShutdown();

//-----------------------------------------------------------------------------
// Native C++ ColorPalette API (callable from ConVar callbacks, other systems)
//-----------------------------------------------------------------------------
bool     ColorPalette_HasBeenInitialized();
int      ColorPalette_GetIDFromString(const char* colorName);
Vector3D ColorPalette_GetColorFromID(int colorID);
Vector3D ColorPalette_GetDefaultColorFromID(int colorID);
bool     ColorPalette_IsColorCustomized(int colorID);
void     ColorPalette_OverrideColorByName(const char* colorName, const Vector3D& color);
void     ColorPalette_ResetColor(const char* colorName);
int      ColorPalette_GetColorCount();
void     ColorPalette_ClampAndValidateColor(Vector3D& result, const Vector3D& color);

#endif // VSCRIPT_COLORPALETTE_H
