#include "CopilotPalette.hpp"
#include "AddOnVersion.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr short MinPaletteClientWidth = 320;
constexpr short MinPaletteClientHeight = 400;
constexpr UInt16 CopilotServerPort = 8765;
constexpr const char* CopilotServerStatusPath = "/api/copilot/status";
constexpr const char* CopilotServerCommand = "/bin/bash /tmp/start_openbrep.sh";
constexpr const char* CopilotDebugLogPath = "/tmp/copilot_debug.log";
constexpr int ServerStartupPollAttempts = 20;
constexpr int ServerStartupPollIntervalMs = 500;

static void AppendCopilotDebugLog (const GS::UniString& message)
{
	std::ofstream logFile (CopilotDebugLogPath, std::ios::out | std::ios::app);
	if (!logFile.is_open ())
		return;
	logFile << message.ToCStr ().Get () << "\n";
}

static GS::UniString FormatDebugMessage (const char* prefix, bool isRunning)
{
	return GS::UniString (prefix) + (isRunning ? "true" : "false");
}

static bool IsCopilotServerRunning ()
{
	const int socketFd = socket (AF_INET, SOCK_STREAM, 0);
	if (socketFd < 0)
		return false;

	sockaddr_in serverAddress {};
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons (CopilotServerPort);
	serverAddress.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

	const bool connected = connect (socketFd, reinterpret_cast<sockaddr*> (&serverAddress), sizeof (serverAddress)) == 0;
	close (socketFd);
	return connected;
}

// Minimal HTTP/1.1 GET to the OpenBrep workbench status endpoint.
// Returns true and stores the response body on HTTP 200, false otherwise.
static bool FetchCopilotStatus (std::string& responseBody)
{
	const int socketFd = socket (AF_INET, SOCK_STREAM, 0);
	if (socketFd < 0)
		return false;

	// Keep an unexpected service on 8765 from blocking Archicad's UI thread.
	const timeval timeout { 2, 0 };
	setsockopt (socketFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof (timeout));
	setsockopt (socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (timeout));

	sockaddr_in serverAddress {};
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons (CopilotServerPort);
	serverAddress.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

	if (connect (socketFd, reinterpret_cast<sockaddr*> (&serverAddress), sizeof (serverAddress)) != 0) {
		close (socketFd);
		return false;
	}

	const std::string request =
		"GET " + std::string (CopilotServerStatusPath) + " HTTP/1.1\r\n"
		"Host: localhost\r\n"
		"Connection: close\r\n"
		"\r\n";
	size_t sentBytes = 0;
	while (sentBytes < request.size ()) {
		const ssize_t sent = send (socketFd, request.data () + sentBytes, request.size () - sentBytes, 0);
		if (sent <= 0) {
			close (socketFd);
			return false;
		}
		sentBytes += static_cast<size_t> (sent);
	}

	std::string rawResponse;
	char buffer[4096];
	ssize_t received = 0;
	while ((received = recv (socketFd, buffer, sizeof (buffer), 0)) > 0) {
		rawResponse.append (buffer, static_cast<size_t> (received));
		if (rawResponse.size () > 64 * 1024) {
			close (socketFd);
			return false;
		}
	}

	close (socketFd);

	const size_t headerEnd = rawResponse.find ("\r\n\r\n");
	if (headerEnd == std::string::npos)
		return false;

	const std::string header = rawResponse.substr (0, headerEnd);
	const size_t statusCodeStart = header.find (' ');
	if (statusCodeStart == std::string::npos || header.size () < statusCodeStart + 4)
		return false;
	if (header.substr (statusCodeStart + 1, 3) != "200")
		return false;

	responseBody = rawResponse.substr (headerEnd + 4);
	return true;
}

// Parses a dotted version string ("0.4.0") into up to three numeric parts.
static bool ParseVersion (const std::string& version, int (&parts)[3])
{
	parts[0] = parts[1] = parts[2] = 0;
	int partIndex = 0;
	int current = 0;
	bool hasDigit = false;

	for (char ch : version) {
		if (ch >= '0' && ch <= '9') {
			current = current * 10 + (ch - '0');
			hasDigit = true;
		} else if (ch == '.' && hasDigit && partIndex < 2) {
			parts[partIndex++] = current;
			current = 0;
			hasDigit = false;
		} else {
			return false;
		}
	}

	if (!hasDigit)
		return false;
	parts[partIndex] = current;
	return true;
}

// Returns true when this Add-On version satisfies the server's min_addon_version.
static bool IsAddOnVersionCompatible (const std::string& minAddonVersion)
{
	int minParts[3];
	if (!ParseVersion (minAddonVersion, minParts))
		return false;

	int addonParts[3];
	if (!ParseVersion (ADDON_VERSION, addonParts))
		return false;

	for (int i = 0; i < 3; ++i) {
		if (addonParts[i] < minParts[i])
			return false;
		if (addonParts[i] > minParts[i])
			return true;
	}
	return true;
}

enum class CopilotServerState {
	NotRunning,			// nothing listens on the port
	RunningIncompatible,// something listens but /api/copilot/status is missing or version mismatch
	RunningCompatible	// workbench is up and version handshake passed
};

// Two-layer probe: fast socket connect, then GET /api/copilot/status and
// validate min_addon_version against this Add-On's ADDON_VERSION.
static CopilotServerState ProbeCopilotServer ()
{
	if (!IsCopilotServerRunning ())
		return CopilotServerState::NotRunning;

	std::string statusBody;
	if (!FetchCopilotStatus (statusBody))
		return CopilotServerState::RunningIncompatible;

	const std::string key = "\"min_addon_version\"";
	const size_t keyPos = statusBody.find (key);
	if (keyPos == std::string::npos)
		return CopilotServerState::RunningIncompatible;

	const size_t colonPos = statusBody.find (':', keyPos);
	if (colonPos == std::string::npos)
		return CopilotServerState::RunningIncompatible;

	const size_t quoteStart = statusBody.find ('"', colonPos + 1);
	if (quoteStart == std::string::npos)
		return CopilotServerState::RunningIncompatible;

	const size_t quoteEnd = statusBody.find ('"', quoteStart + 1);
	if (quoteEnd == std::string::npos)
		return CopilotServerState::RunningIncompatible;

	const std::string minAddonVersion = statusBody.substr (quoteStart + 1, quoteEnd - quoteStart - 1);
	return IsAddOnVersionCompatible (minAddonVersion) ? CopilotServerState::RunningCompatible
													  : CopilotServerState::RunningIncompatible;
}

static void CreateStartScriptIfNeeded ()
{
	const std::string scriptPath = "/tmp/start_openbrep.sh";
	std::ifstream testFile (scriptPath.c_str ());
	if (testFile.good ()) {
		std::string line;
		while (std::getline (testFile, line)) {
			if (line == "# openbrep-start-script-v1")
				return;
		}
	}
	testFile.close ();

	std::ofstream scriptFile (scriptPath, std::ios::out | std::ios::trunc);
	if (!scriptFile.is_open ()) {
		AppendCopilotDebugLog ("CreateStartScriptIfNeeded: failed to open script file for writing");
		return;
	}

	scriptFile << "#!/bin/bash\n";
	scriptFile << "# openbrep-start-script-v1\n";
	scriptFile << "set -euo pipefail\n";
	scriptFile << "\n";
	// GUI-launched Archicad normally does not inherit Homebrew's PATH.
	scriptFile << "export PATH=\"/opt/homebrew/bin:/usr/local/bin:$PATH\"\n";
	scriptFile << "if ! command -v obr >/dev/null 2>&1; then\n";
	scriptFile << "  echo \"obr not found in PATH\" >> /tmp/openbrep_serve.log\n";
	scriptFile << "  exit 1\n";
	scriptFile << "fi\n";
	scriptFile << "nohup obr serve >> /tmp/openbrep_serve.log 2>&1 &\n";
	scriptFile.close ();

	chmod (scriptPath.c_str (), 0755);
	AppendCopilotDebugLog ("CreateStartScriptIfNeeded: script created");
}

// Launches the OpenBrep workbench when it is not running and polls until it is
// ready (or the ~10s budget is exhausted). Returns the final observed state.
static CopilotServerState EnsureCopilotServerRunning ()
{
	const CopilotServerState runningBefore = ProbeCopilotServer ();
	AppendCopilotDebugLog (FormatDebugMessage ("EnsureCopilotServerRunning: before=", runningBefore == CopilotServerState::RunningCompatible));
	if (runningBefore != CopilotServerState::NotRunning)
		return runningBefore;

	CreateStartScriptIfNeeded ();

	AppendCopilotDebugLog ("EnsureCopilotServerRunning: launching /tmp/start_openbrep.sh");
	std::system (CopilotServerCommand);

	for (int attempt = 0; attempt < ServerStartupPollAttempts; ++attempt) {
		std::this_thread::sleep_for (std::chrono::milliseconds (ServerStartupPollIntervalMs));
		const CopilotServerState state = ProbeCopilotServer ();
		AppendCopilotDebugLog (FormatDebugMessage ("EnsureCopilotServerRunning: poll=", state == CopilotServerState::RunningCompatible));
		if (state != CopilotServerState::NotRunning)
			return state;
	}

	return CopilotServerState::NotRunning;
}

static GS::UniString GetWorkbenchUrl ()
{
	return GS::UniString ("http://localhost:8765/?mode=copilot&addon_version=") + GS::UniString (ADDON_VERSION);
}

// Guidance page shown when the workbench cannot be reached within the wait budget.
static GS::UniString BuildFallbackHtml ()
{
	return GS::UniString (R"(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<style>
  body { font-family: -apple-system, "PingFang SC", sans-serif; background: #f5f6f8; margin: 0; padding: 24px; color: #1f2329; }
  h2 { font-size: 18px; margin: 0 0 12px; }
  p { font-size: 14px; line-height: 1.7; margin: 8px 0; }
  code { background: #e8eaed; border-radius: 4px; padding: 2px 6px; font-size: 13px; }
</style>
</head>
<body>
<h2>OpenBrep 后台服务未运行</h2>
<p>请先启动 OpenBrep 后台服务：在终端运行 <code>obr serve</code></p>
<p>日志路径：<code>/tmp/openbrep_serve.log</code> 与 <code>~/.openbrep/logs/obr7.log</code></p>
</body>
</html>)", CC_UTF8);
}

// Prompt shown when the workbench version handshake fails.
static GS::UniString BuildUpgradePromptHtml ()
{
	return GS::UniString (R"(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<style>
  body { font-family: -apple-system, "PingFang SC", sans-serif; background: #f5f6f8; margin: 0; padding: 24px; color: #1f2329; }
  h2 { font-size: 18px; margin: 0 0 12px; }
  p { font-size: 14px; line-height: 1.7; margin: 8px 0; }
</style>
</head>
<body>
<h2>版本不兼容</h2>
<p>请升级 OpenBrep 或 Add-On 后重试。</p>
</body>
</html>)", CC_UTF8);
}

static void ConfigureInitialPaletteSize (CopilotPalette& palette)
{
	const DG::NativeRect visibleBounds = DG::VisibleBoundingRectOfScreens ();
	const int screenWidth = visibleBounds.GetWidth ().GetValue ();
	const int screenHeight = visibleBounds.GetHeight ().GetValue ();

	const short targetWidth = static_cast<short> (std::max (static_cast<int> (MinPaletteClientWidth), screenWidth / 3));
	const short targetHeight = static_cast<short> (std::max (static_cast<int> (MinPaletteClientHeight), (screenHeight * 4) / 5));

	palette.SetGrowType (DG::Dialog::HVGrow);
	palette.SetMinClientSize (MinPaletteClientWidth, MinPaletteClientHeight);
	palette.SetClientSize (targetWidth, targetHeight);
	palette.KeepInScreen ();
}

static GSErrCode NotificationHandler (API_NotifyEventID notifID, Int32 /*param*/)
{
	switch (notifID) {
		case APINotify_Quit:
			CopilotPalette::DestroyInstance ();
			break;
		default:
			break;
	}

	return NoError;
}

} // namespace

const GS::Guid CopilotPalette::paletteGuid ("{C95C786A-4B83-4D0A-8C47-A64075461E48}");
GS::Ref<CopilotPalette> CopilotPalette::instance;

CopilotPalette::CopilotPalette () :
	DG::Palette (ACAPI_GetOwnResModule (), CopilotPaletteResId, ACAPI_GetOwnResModule (), paletteGuid),
	browser (GetReference (), BrowserId)
{
	ACAPI_ProjectOperation_CatchProjectEvent (APINotify_Quit, NotificationHandler);
	Attach (*this);
	ConfigureInitialPaletteSize (*this);
	browser.Move (0, 0);
	browser.SetSize (GetClientWidth (), GetClientHeight ());
	BeginEventProcessing ();
}

CopilotPalette::~CopilotPalette ()
{
	EndEventProcessing ();
}

bool CopilotPalette::HasInstance ()
{
	return instance != nullptr;
}

void CopilotPalette::CreateInstance ()
{
	if (!HasInstance ()) {
		instance = new CopilotPalette ();
		ACAPI_KeepInMemory (true);
	}
}

CopilotPalette& CopilotPalette::Instance ()
{
	DBASSERT (HasInstance ());
	return *instance;
}

void CopilotPalette::DestroyInstance ()
{
	instance = nullptr;
}

void CopilotPalette::Show ()
{
	// A palette close only hides this instance. Probe and reload on every reopen
	// so stopping the backend while hidden produces the fallback page as expected.
	InitBrowserControl ();
	DG::Palette::Show ();
	BringToFront ();
}

void CopilotPalette::Hide ()
{
	DG::Palette::Hide ();
}

void CopilotPalette::InitBrowserControl ()
{
	const CopilotServerState state = EnsureCopilotServerRunning ();

	switch (state) {
		case CopilotServerState::RunningCompatible:
			AppendCopilotDebugLog ("InitBrowserControl: workbench ready, loading copilot page");
			browser.LoadURL (GetWorkbenchUrl ());
			break;
		case CopilotServerState::RunningIncompatible:
			AppendCopilotDebugLog ("InitBrowserControl: version handshake failed, showing upgrade prompt");
			browser.LoadHTML (BuildUpgradePromptHtml ());
			break;
		case CopilotServerState::NotRunning:
			AppendCopilotDebugLog ("InitBrowserControl: workbench not reachable, showing fallback page");
			browser.LoadHTML (BuildFallbackHtml ());
			break;
	}
}

void CopilotPalette::PanelResized (const DG::PanelResizeEvent&)
{
	BeginMoveResizeItems ();
	browser.SetSize (GetClientWidth (), GetClientHeight ());
	EndMoveResizeItems ();
}

void CopilotPalette::PanelCloseRequested (const DG::PanelCloseRequestEvent&, bool* accepted)
{
	Hide ();
	*accepted = true;
}

GSErrCode CopilotPalette::PaletteControlCallBack (Int32, API_PaletteMessageID messageID, GS::IntPtr param)
{
	switch (messageID) {
		case APIPalMsg_OpenPalette:
			if (!HasInstance ())
				CreateInstance ();
			Instance ().Show ();
			break;

		case APIPalMsg_ClosePalette:
			if (!HasInstance ())
				break;
			Instance ().Hide ();
			break;

		case APIPalMsg_HidePalette_Begin:
			if (HasInstance () && Instance ().IsVisible ())
				Instance ().Hide ();
			break;

		case APIPalMsg_HidePalette_End:
			if (HasInstance () && !Instance ().IsVisible ())
				Instance ().Show ();
			break;

		case APIPalMsg_DisableItems_Begin:
			if (HasInstance () && Instance ().IsVisible ())
				Instance ().DisableItems ();
			break;

		case APIPalMsg_DisableItems_End:
			if (HasInstance () && Instance ().IsVisible ())
				Instance ().EnableItems ();
			break;

		case APIPalMsg_IsPaletteVisible:
			*(reinterpret_cast<bool*> (param)) = HasInstance () && Instance ().IsVisible ();
			break;

		case APIPalMsg_GetPaletteDeactivationMethod:
			*(reinterpret_cast<API_PaletteDeactivationMethod*> (param)) = APIPaletteDeactivationMethod_DisableItems;
			break;

		default:
			break;
	}

	return NoError;
}

GSErrCode CopilotPalette::RegisterPaletteControlCallBack ()
{
	return ACAPI_RegisterModelessWindow (
		GS::CalculateHashValue (paletteGuid),
		PaletteControlCallBack,
		API_PalEnabled_FloorPlan + API_PalEnabled_Section + API_PalEnabled_Elevation +
		API_PalEnabled_InteriorElevation + API_PalEnabled_3D + API_PalEnabled_Detail +
		API_PalEnabled_Worksheet + API_PalEnabled_Layout + API_PalEnabled_DocumentFrom3D,
		GSGuid2APIGuid (paletteGuid));
}
