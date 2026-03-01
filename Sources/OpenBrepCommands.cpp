#include "OpenBrepCommands.hpp"
#include "AddOnVersion.hpp"

#include <cstdlib>
#include <string>

namespace {

static void LaunchOpenBrepInBrowser ()
{
	const std::string url = "http://localhost:8501";
#if defined (macintosh)
	const std::string command = "open " + url;
#elif defined (WINDOWS)
	const std::string command = "start " + url;
#else
	const std::string command = "xdg-open " + url;
#endif
	std::system (command.c_str ());
}

} // namespace

GSErrCode OpenBrepMenuCommandHandler (const API_MenuParams* menuParams)
{
	if (menuParams->menuItemRef.menuResID == OpenBrepMenuResId &&
		menuParams->menuItemRef.itemIndex == OpenBrepMenuItemIndex)
	{
		LaunchOpenBrepInBrowser ();
	}

	return NoError;
}

