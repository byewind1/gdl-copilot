#include "APIEnvir.h"
#include "ACAPinc.h"

#include "OpenBrepCommands.hpp"
#include "CopilotPalette.hpp"
#include "EvaluateLibraryPartCommand.hpp"

API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, 32000, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, 32000, 2, ACAPI_GetOwnResModule ());

	// APIAddon_Normal 时 AC29 会延迟调用 Initialize（用到才初始化），
	// Add-On Commands（EvaluateLibraryPart 等）将一直查不到。
	// Preload 要求启动时完整加载并初始化——命令注册必须在启动时完成。
	return APIAddon_Preload;
}

GSErrCode RegisterInterface (void)
{
	GSErrCode err = ACAPI_MenuItem_RegisterMenu (OpenBrepMenuResIdLaunch, 0, MenuCode_UserDef, MenuFlag_Default);
	if (DBERROR (err != NoError))
		return err;

	err = ACAPI_MenuItem_RegisterMenu (OpenBrepMenuResIdCopilot, 0, MenuCode_UserDef, MenuFlag_Default);
	if (DBERROR (err != NoError))
		return err;

	return NoError;
}

GSErrCode Initialize (void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (OpenBrepMenuResIdLaunch, OpenBrepMenuCommandHandler);
	if (DBERROR (err != NoError))
		return err;

	err = ACAPI_MenuItem_InstallMenuHandler (OpenBrepMenuResIdCopilot, OpenBrepMenuCommandHandler);
	if (DBERROR (err != NoError))
		return err;

	err = CopilotPalette::RegisterPaletteControlCallBack ();
	if (DBERROR (err != NoError))
		return err;

	// 权威预览命令（OpenBrep.EvaluateLibraryPart）：工作台经 Archicad
	// Add-On Commands HTTP 端点调用
	{
		GS::Owner<EvaluateLibraryPartCommand> command = GS::NewOwned<EvaluateLibraryPartCommand> ();
		err = ACAPI_AddOnAddOnCommunication_InstallAddOnCommandHandler (command.Pass ());
		if (DBERROR (err != NoError))
			return err;
	}

	return NoError;
}

GSErrCode FreeData (void)
{
	return NoError;
}
