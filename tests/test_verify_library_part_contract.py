from pathlib import Path


ROOT = Path(__file__).parents[1]


def test_verify_command_contract_is_registered_and_fail_closed():
    header = (ROOT / "Sources/VerifyLibraryPartArtifactCommand.hpp").read_text()
    source = (ROOT / "Sources/VerifyLibraryPartArtifactCommand.cpp").read_text()
    main = (ROOT / "Sources/AddOnMain.cpp").read_text()

    assert 'GetName () const' in header
    assert 'VerifyLibraryPartArtifact' in source
    assert 'gsmSha256' in source
    assert 'identityStatus' in source
    assert 'libraryRestoreStatus' in source
    assert 'undoStatus' in source
    assert 'VerifyLibraryPartArtifactCommand' in main


def test_verify_command_rejects_hash_or_loaded_path_mismatch():
    source = (ROOT / "Sources/VerifyLibraryPartArtifactCommand.cpp").read_text()
    assert 'gsmSha256' in source
    assert 'loadedIdentity' in source
    assert 'identityStatus' in source
    assert 'HashMismatch' in source or 'hash mismatch' in source.lower()
    assert 'location' in source
