#pragma once

#include "APIEnvir.h"
#include "ACAPinc.h"

constexpr short OpenBrepMenuResIdLaunch = 32500;
constexpr short OpenBrepMenuResIdCopilot = 32501;

GSErrCode OpenBrepMenuCommandHandler (const API_MenuParams* menuParams);
