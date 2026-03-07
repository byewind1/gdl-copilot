#include "CopilotPalette.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr short MinPaletteClientWidth = 320;
constexpr short MinPaletteClientHeight = 400;
constexpr UInt16 CopilotServerPort = 8502;
constexpr const char* CopilotServerUrl = "http://localhost:8502";
constexpr const char* CopilotServerCommand = "/bin/bash -c 'cd /Users/ren/MAC工作/工作/code/开源项目/openbrep-addon && nohup /Users/ren/miniconda3/bin/python -m uvicorn copilot.server:app --port 8502 > /tmp/copilot.log 2>&1 &'";

static bool IsCopilotServerRunning ()
{
	int socketFd = socket (AF_INET, SOCK_STREAM, 0);
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

static void EnsureCopilotServerRunning ()
{
	if (IsCopilotServerRunning ())
		return;

	std::system (CopilotServerCommand);

	for (int attempt = 0; attempt < 8; ++attempt) {
		std::this_thread::sleep_for (std::chrono::milliseconds (250));
		if (IsCopilotServerRunning ())
			break;
	}
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
	InitBrowserControl ();
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
	DG::Palette::Show ();
	BringToFront ();
}

void CopilotPalette::Hide ()
{
	DG::Palette::Hide ();
}

void CopilotPalette::InitBrowserControl ()
{
	EnsureCopilotServerRunning ();
	browser.LoadURL (CopilotServerUrl);
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
