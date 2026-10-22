/* A directory grant must not authorize overwriting a denied child. */
#pragma strict_types

void
create()
{
    mkdir("/tests/security-target");
    rm("/tests/protected");
    write_file("/tests/protected", "source\n");
    if (rename("/tests/protected", "/tests/security-target") != 0 ||
        read_file("/tests/protected") != "source\n")
        throw("Derived rename target bypassed policy\n");
    if (rename("/tests/protected", "/tests/security-target/allowed") != 1 ||
        read_file("/tests/security-target/allowed") != "source\n")
        throw("Authorized rename failed\n");
    rm("/tests/security-target/allowed");
    rmdir("/tests/security-target");
}
