#include "OpenBrepCommands.hpp"
#include "AddOnVersion.hpp"
#include "CopilotPalette.hpp"

#include <cstdlib>

namespace {

static void LaunchOpenBrepInBrowser ()
{
	std::system ("open http://localhost:8501");
}

static void LaunchGdlCopilot ()
{
	if (!CopilotPalette::HasInstance ())
		CopilotPalette::CreateInstance ();

	CopilotPalette::Instance ().Show ();
}

} // namespace

GSErrCode OpenBrepMenuCommandHandler (const API_MenuParams* menuParams)
{
	if (menuParams->menuItemRef.menuResID == OpenBrepMenuResIdLaunch) {
		LaunchOpenBrepInBrowser ();
	} else if (menuParams->menuItemRef.menuResID == OpenBrepMenuResIdCopilot) {
		LaunchGdlCopilot ();
	}

	return NoError;
}

