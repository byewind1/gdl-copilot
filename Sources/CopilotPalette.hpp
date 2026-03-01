#pragma once

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "DGModule.hpp"
#include "DGBrowser.hpp"

constexpr short CopilotPaletteResId = 32510;

class CopilotPalette final : public DG::Palette,
	public DG::PanelObserver
{
private:
	enum {
		BrowserId = 1
	};

	DG::Browser browser;

	static GS::Ref<CopilotPalette> instance;
	static const GS::Guid paletteGuid;

	CopilotPalette ();
	void InitBrowserControl ();

	virtual void PanelResized (const DG::PanelResizeEvent& ev) override;
	virtual void PanelCloseRequested (const DG::PanelCloseRequestEvent& ev, bool* accepted) override;

	static GSErrCode PaletteControlCallBack (Int32 paletteId, API_PaletteMessageID messageID, GS::IntPtr param);

public:
	virtual ~CopilotPalette ();

	static bool HasInstance ();
	static void CreateInstance ();
	static CopilotPalette& Instance ();
	static void DestroyInstance ();

	void Show ();
	void Hide ();

	static GSErrCode RegisterPaletteControlCallBack ();
};
