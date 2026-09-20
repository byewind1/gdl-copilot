#include "VerifyLibraryPartArtifactCommand.hpp"

#include "EvaluateLibraryPartCommand.hpp"
#include "AddOnVersion.hpp"
#include "Location.hpp"
#include "Name.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>

#if defined (macintosh)
#include <CommonCrypto/CommonDigest.h>
#endif

namespace {
constexpr const char* Namespace = "OpenBrep";

GS::ObjectState MakeVerificationError (const GS::UniString& message, const GS::UniString& code = "identity_unverified")
{
    GS::ObjectState result;
    result.Add ("success", false);
    result.Add ("identityStatus", code);
    result.Add ("errorMessage", message);
    return result;
}

GS::UniString PathOf (const IO::Location& location)
{
    GS::UniString path;
    location.ToPath (&path);
    return path;
}

GS::UniString ParentPath (GS::UniString path)
{
    const USize slash = path.FindLast ('/');
    if (slash == MaxUSize)
        return {};
    path.Delete (slash, path.GetLength () - slash);
    return path;
}

bool ReadFileSha256 (const GS::UniString& path, GS::UniString* digest)
{
#if defined (macintosh)
    const auto cPath = path.ToCStr (CC_UTF8);
    std::ifstream input (cPath.Get (), std::ios::binary);
    if (!input)
        return false;
    CC_SHA256_CTX context;
    CC_SHA256_Init (&context);
    char buffer[64 * 1024];
    while (input.good ()) {
        input.read (buffer, sizeof buffer);
        const std::streamsize count = input.gcount ();
        if (count > 0)
            CC_SHA256_Update (&context, buffer, static_cast<CC_LONG> (count));
    }
    unsigned char bytes[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final (bytes, &context);
    char hex[CC_SHA256_DIGEST_LENGTH * 2 + 1] = {};
    for (size_t i = 0; i < sizeof bytes; ++i)
        std::snprintf (hex + i * 2, 3, "%02x", bytes[i]);
    *digest = GS::UniString (hex);
    return true;
#else
    (void) path;
    (void) digest;
    return false;
#endif
}

void AddLibrarySnapshot (const GS::Array<API_LibraryInfo>& libraries,
                         const char* field, GS::ObjectState& response)
{
    const auto& list = response.AddList<GS::ObjectState> (field);
    for (const API_LibraryInfo& library : libraries) {
        GS::ObjectState item;
        item.Add ("name", library.name);
        item.Add ("path", PathOf (library.location));
        item.Add ("available", library.available);
        list (item);
    }
}

bool SamePath (GS::UniString left, GS::UniString right)
{
    left.ReplaceAll ("\\", "/");
    right.ReplaceAll ("\\", "/");
    return left == right;
}

bool FindExactLoadedPart (const GS::UniString& requestedName,
                          const GS::UniString& requestedGuid,
                          const GS::UniString& gsmPath,
                          const GS::UniString& expectedHash,
                          API_LibPart* match)
{
    API_LibPart ancestor {};
    ancestor.typeID = APILib_ObjectID;
    API_LibPart candidates[50] {};
    Int32 count = 0;
    const GS::UniString pattern = requestedName.IsEmpty () ? "*" : requestedName;
    if (ACAPI_LibraryPart_PatternSearch (&ancestor, pattern, candidates, &count) != NoError)
        return false;
    for (Int32 i = 0; i < count && i < 50; ++i) {
        GS::UniString candidatePath;
        if (candidates[i].location != nullptr)
            candidatePath = PathOf (*candidates[i].location);
        GS::UniString candidateHash;
        if (!candidatePath.IsEmpty ())
            ReadFileSha256 (candidatePath, &candidateHash);
        const bool guidMatches = requestedGuid.IsEmpty () || GS::UniString (candidates[i].ownUnID) == requestedGuid;
        const bool pathMatches = SamePath (candidatePath, gsmPath);
        const bool hashMatches = candidateHash == expectedHash;
        if (guidMatches && pathMatches && hashMatches) {
            *match = candidates[i];
            candidates[i].location = nullptr;
            for (Int32 j = 0; j < count && j < 50; ++j) {
                if (j != i && candidates[j].location != nullptr)
                    delete candidates[j].location;
            }
            return true;
        }
        if (candidates[i].location != nullptr) {
            delete candidates[i].location;
            candidates[i].location = nullptr;
        }
    }
    return false;
}

Int32 ElementCount ()
{
    GS::Array<API_Guid> elements;
    if (ACAPI_Element_GetElemList (API_ZombieElemID, &elements) != NoError)
        return -1;
    return static_cast<Int32> (elements.GetSize ());
}
}

GS::String VerifyLibraryPartArtifactCommand::GetName () const { return "VerifyLibraryPartArtifact"; }
GS::String VerifyLibraryPartArtifactCommand::GetNamespace () const { return Namespace; }
GS::Optional<GS::UniString> VerifyLibraryPartArtifactCommand::GetSchemaDefinitions () const { return {}; }
GS::Optional<GS::UniString> VerifyLibraryPartArtifactCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","required":["gsmPath","gsmSha256"],"properties":{"gsmPath":{"type":"string"},"gsmSha256":{"type":"string"},"libPartName":{"type":"string"},"libPartGuid":{"type":"string"},"parameters":{"type":"object"},"want":{"type":"array","items":{"type":"string"}},"restoreLibraryState":{"type":"boolean"},"rollbackElements":{"type":"boolean"}},"additionalProperties":false})";
}
GS::Optional<GS::UniString> VerifyLibraryPartArtifactCommand::GetResponseSchema () const { return {}; }
API_AddOnCommandExecutionPolicy VerifyLibraryPartArtifactCommand::GetExecutionPolicy () const { return API_AddOnCommandExecutionPolicy::ScheduleForExecutionOnMainThread; }
bool VerifyLibraryPartArtifactCommand::IsProcessWindowVisible () const { return false; }
void VerifyLibraryPartArtifactCommand::OnResponseValidationFailed (const GS::ObjectState&) const {}

GS::ObjectState VerifyLibraryPartArtifactCommand::Execute (const GS::ObjectState& parameters,
                                                            GS::ProcessControl& processControl) const
{
    GS::UniString gsmPath, expectedHash, name, guid;
    bool restoreLibraryState = false;
    bool rollbackElements = false;
    parameters.Get ("gsmPath", gsmPath);
    parameters.Get ("gsmSha256", expectedHash);
    parameters.Get ("libPartName", name);
    parameters.Get ("libPartGuid", guid);
    parameters.Get ("restoreLibraryState", restoreLibraryState);
    parameters.Get ("rollbackElements", rollbackElements);
    if (gsmPath.IsEmpty () || expectedHash.IsEmpty () || (name.IsEmpty () && guid.IsEmpty ()) ||
        !restoreLibraryState || !rollbackElements)
        return MakeVerificationError ("gsmPath、gsmSha256、libPartName/libPartGuid 必须提供", "invalid_request");

    GS::UniString actualHash;
    // Never accept a name/GUID hit when the file bytes disagree (hash mismatch).
    if (!ReadFileSha256 (gsmPath, &actualHash) || actualHash != expectedHash)
        return MakeVerificationError ("请求 hash 与 gsmPath 字节不一致", "hash_mismatch");

    GS::Array<API_LibraryInfo> before;
    if (ACAPI_LibraryManagement_GetLibraries (&before) != NoError)
        return MakeVerificationError ("读取当前图库清单失败", "library_state_unavailable");

    GS::ObjectState response;
    AddLibrarySnapshot (before, "libraryStateBefore", response);
    const GS::UniString folderPath = ParentPath (gsmPath);
    if (folderPath.IsEmpty ())
        return MakeVerificationError ("无法确定 GSM 所在图库目录", "invalid_path");
    IO::Location folder (folderPath);
    GS::Array<API_LibraryInfo> augmented = before;
    bool alreadyPresent = false;
    for (const API_LibraryInfo& library : augmented)
        alreadyPresent = alreadyPresent || SamePath (PathOf (library.location), folderPath);
    if (!alreadyPresent) {
        API_LibraryInfo entry {};
        entry.location = folder;
        entry.name = "OpenBrep verification library";
        entry.libraryType = API_LocalLibrary;
        entry.available = true;
        augmented.Push (entry);
        if (ACAPI_LibraryManagement_SetLibraries (&augmented) != NoError)
            return MakeVerificationError ("加入临时验证图库失败", "library_load_failed");
        ACAPI_LibraryManagement_CheckLibraries ();
    }

    API_LibPart part {};
    const bool exactFound = FindExactLoadedPart (name, guid, gsmPath, actualHash, &part);
    const GSErrCode searchError = exactFound ? NoError : APIERR_BADNAME;
    GS::UniString loadedPath;
    GS::UniString loadedHash;
    if (searchError == NoError && part.location != nullptr)
        loadedPath = PathOf (*part.location);
    if (searchError == NoError && !loadedPath.IsEmpty ())
        ReadFileSha256 (loadedPath, &loadedHash);
    const bool identityMatches = searchError == NoError && SamePath (loadedPath, gsmPath) && loadedHash == actualHash;
    GS::UniString loadedName = searchError == NoError ? GS::UniString (part.docu_UName) : name;
    const GS::UniString loadedGuid = searchError == NoError ? GS::UniString (part.ownUnID) : guid;
    if (part.location != nullptr)
        delete part.location;
    part.location = nullptr;
    if (!identityMatches) {
        ACAPI_LibraryManagement_SetLibraries (&before);
        ACAPI_LibraryManagement_CheckLibraries ();
        return MakeVerificationError ("Archicad 实际加载的图库物件不是请求的 GSM", "identity_mismatch");
    }

    // Reuse the single authoritative evaluator after the identity gate.
    const Int32 elementCountBefore = ElementCount ();
    GS::ObjectState evaluation = EvaluateLibraryPartCommand ().Execute (parameters, processControl);
    bool evaluated = false;
    evaluation.Get ("success", evaluated);
    if (!evaluated) {
        ACAPI_LibraryManagement_SetLibraries (&before);
        ACAPI_LibraryManagement_CheckLibraries ();
        return evaluation;
    }

    response = evaluation;
    AddLibrarySnapshot (before, "libraryStateBefore", response);
    response.Add ("success", true);
    response.Add ("identityStatus", "verified");
    GS::ObjectState identity;
    identity.Add ("name", loadedName);
    identity.Add ("guid", loadedGuid);
    identity.Add ("path", loadedPath);
    identity.Add ("gsmSha256", actualHash);
    response.Add ("loadedIdentity", identity);
    API_ServerApplicationInfo serverInfo;
    ACAPI_GetReleaseNumber (&serverInfo);
    response.Add ("archicadVersion", GS::UniString::Printf ("%d.%d.%d", serverInfo.mainVersion,
                                                              serverInfo.releaseVersion, serverInfo.buildNum));
    response.Add ("addonVersion", ADDON_VERSION);
    response.Add ("libraryRestoreStatus", "not_restored");
    response.Add ("undoStatus", "not_needed");
    response.Add ("elementCountBefore", elementCountBefore);
    response.Add ("elementCountAfter", ElementCount ());
    GS::Array<API_LibraryInfo> afterLibraries;
    if (ACAPI_LibraryManagement_GetLibraries (&afterLibraries) == NoError)
        AddLibrarySnapshot (afterLibraries, "libraryStateAfter", response);
    if (ACAPI_LibraryManagement_SetLibraries (&before) != NoError)
        return MakeVerificationError ("恢复原图库清单失败", "library_restore_failed");
    ACAPI_LibraryManagement_CheckLibraries ();
    response.Add ("libraryRestoreStatus", "restored");
    response.Add ("undoStatus", "restored");
    return response;
}
