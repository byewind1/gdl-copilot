#include "CopilotPalette.hpp"

const GS::Guid CopilotPalette::paletteGuid ("{C95C786A-4B83-4D0A-8C47-A64075461E48}");
GS::Ref<CopilotPalette> CopilotPalette::instance;

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

CopilotPalette::CopilotPalette () :
	DG::Palette (ACAPI_GetOwnResModule (), CopilotPaletteResId, ACAPI_GetOwnResModule (), paletteGuid),
	browser (GetReference (), BrowserId)
{
	ACAPI_ProjectOperation_CatchProjectEvent (APINotify_Quit, NotificationHandler);
	Attach (*this);
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
}

void CopilotPalette::Hide ()
{
	DG::Palette::Hide ();
}

void CopilotPalette::InitBrowserControl ()
{
	browser.LoadURL ("http://localhost:8502");
}

void CopilotPalette::PanelResized (const DG::PanelResizeEvent& ev)
{
	BeginMoveResizeItems ();
	browser.Resize (ev.GetHorizontalChange (), ev.GetVerticalChange ());
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
