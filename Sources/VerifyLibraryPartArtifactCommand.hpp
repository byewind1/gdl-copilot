#pragma once

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "ObjectState.hpp"

// Host acceptance command.  Unlike EvaluateLibraryPart this command is only
// green when Archicad has loaded the exact GSM bytes named by gsmPath.
class VerifyLibraryPartArtifactCommand : public API_AddOnCommand {
public:
    GS::String GetName () const override;
    GS::String GetNamespace () const override;
    GS::Optional<GS::UniString> GetSchemaDefinitions () const override;
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetResponseSchema () const override;
    API_AddOnCommandExecutionPolicy GetExecutionPolicy () const override;
    bool IsProcessWindowVisible () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
    void OnResponseValidationFailed (const GS::ObjectState&) const override;
};
