#include "tier0/platform_internal.h"
#include "tier0/dbg.h"
#include "tier1/strtools.h"

//-----------------------------------------------------------------------------
// Purpose: checks if the URL path contains a file extension that isn't a
//          safe web-page extension. URLs with no extension are allowed.
// Input  : *urlText -
// Output : true if the URL should be blocked
//-----------------------------------------------------------------------------
static bool Plat_HasDisallowedExtension(const char* urlText)
{
	static const char* const s_AllowedExtensions[] =
	{
		".htm", ".html", ".php", ".asp", ".aspx", ".jsp", ".cgi",
	};

	// Skip past "scheme://host" to the path portion.
	const char* pPath = V_strstr(urlText, "://");
	if (pPath)
		pPath = V_strstr(pPath + 3, "/");

	if (!pPath)
		return false; // No path (e.g. "https://google.com") — allow.

	// Trim query string and fragment from the path.
	size_t pathLen = V_strlen(pPath);
	const char* pTrim = V_strnchr(pPath, '?', pathLen);
	const char* pHash = V_strnchr(pPath, '#', pathLen);

	if (pTrim && pHash)
		pTrim = (pHash < pTrim) ? pHash : pTrim;
	else if (pHash)
		pTrim = pHash;

	const char* pEnd = pTrim ? pTrim : pPath + pathLen;

	// Find the last '.' after the last '/'.
	const char* pDot = nullptr;
	for (const char* p = pEnd - 1; p >= pPath; p--)
	{
		if (*p == '/')
			break; // No dot in the final segment.
		if (*p == '.')
		{
			pDot = p;
			break;
		}
	}

	if (!pDot)
		return false; // No extension — allow.

	// Block if the extension doesn't match an allowed web-page extension.
	const size_t extLen = static_cast<size_t>(pEnd - pDot);

	for (size_t i = 0; i < V_ARRAYSIZE(s_AllowedExtensions); i++)
	{
		if (extLen == V_strlen(s_AllowedExtensions[i]) &&
			V_strnicmp(pDot, s_AllowedExtensions[i], extLen) == 0)
			return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: sanitizes URL before passing to platform web browser launcher.
//          blocks non-http(s) schemes to prevent RCE via file:// or other
//          dangerous protocols. Even for valid URLs, we avoid the engine's
//          ShellExecuteA fallback and open the default browser directly.
// Input  : *urlText - 
//          flags - 
//-----------------------------------------------------------------------------
static void _Plat_LaunchExternalWebBrowser(const char* urlText, unsigned int flags)
{
	if (!urlText || !*urlText)
		return;
		
	if (V_strnicmp(urlText, "http://", 7) != 0 &&
		V_strnicmp(urlText, "https://", 8) != 0)
	{
		Warning(eDLL_T::ENGINE,
			"Blocked LaunchExternalWebBrowser call with disallowed scheme!\n"
			" URL: '%.128s'\n"
			" Only 'http://' and 'https://' URLs are permitted.\n",
			urlText);
		return;
	}

	if (Plat_HasDisallowedExtension(urlText))
	{
		Warning(eDLL_T::ENGINE,
			"Blocked LaunchExternalWebBrowser call with disallowed file extension!\n"
			" URL: '%.128s'\n",
			urlText);
		return;
	}

	// Pass through to the original; Origin/Steam overlays will handle it if
	// available. If neither is active the engine falls back to ShellExecuteA,
	// which is safe here because we've already validated the scheme above —
	// ShellExecuteA with "open" on an http(s) URL just launches the default
	// browser, and the browser's own security handles any server redirects.
	v_Plat_LaunchExternalWebBrowser(urlText, flags);
}

//-----------------------------------------------------------------------------
// Purpose: gets the process up time in seconds
// Output : double
//-----------------------------------------------------------------------------
double Plat_FloatTime()
{
	return v_Plat_FloatTime();
}

//-----------------------------------------------------------------------------
// Purpose: gets the process up time in milliseconds
// Output : uint64_t
//-----------------------------------------------------------------------------
uint64_t Plat_MSTime()
{
	return v_Plat_MSTime();
}

//-----------------------------------------------------------------------------
// Purpose: gets the process up time ( !! INTERNAL ONLY !! DO NOT USE !! ).
// Output : const char*
//-----------------------------------------------------------------------------
const char* Plat_GetProcessUpTime()
{
	static char szBuf[4096];
	sprintf_s(szBuf, sizeof(szBuf), "[%.3f] ", v_Plat_FloatTime ? Plat_FloatTime() : 0.0);

	return szBuf;
}

//-----------------------------------------------------------------------------
// Purpose: gets the process up time.
// Input  : *szBuf - 
//			nSize - 
//-----------------------------------------------------------------------------
void Plat_GetProcessUpTime(char* szBuf, size_t nSize)
{
	sprintf_s(szBuf, nSize, "[%.3f] ", v_Plat_FloatTime ? Plat_FloatTime() : 0.0);
}

void VPlatform::Detour(const bool bAttach) const
{
	DetourSetup(&v_Plat_LaunchExternalWebBrowser, &_Plat_LaunchExternalWebBrowser, bAttach);
}