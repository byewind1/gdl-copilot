#pragma once

#include "APIEnvir.h"
#include "ACAPinc.h"

constexpr short OpenBrepMenuResId = 32500;
constexpr short OpenBrepMenuItemIndex = 1;

GSErrCode OpenBrepMenuCommandHandler (const API_MenuParams* menuParams);
