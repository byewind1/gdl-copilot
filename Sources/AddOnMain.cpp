#include "APIEnvir.h"
#include "ACAPinc.h"

#include "OpenBrepCommands.hpp"

API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, 32000, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, 32000, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}

GSErrCode RegisterInterface (void)
{
	GSErrCode err = ACAPI_MenuItem_RegisterMenu (OpenBrepMenuResId, 0, MenuCode_UserDef, MenuFlag_Default);
	if (DBERROR (err != NoError))
		return err;

	return NoError;
}

GSErrCode Initialize (void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (OpenBrepMenuResId, OpenBrepMenuCommandHandler);
	if (DBERROR (err != NoError))
		return err;

	return NoError;
}

GSErrCode FreeData (void)
{
	return NoError;
}
