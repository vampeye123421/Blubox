#pragma once

// Returns true if an updated Studio process was started and the current
// process should exit. Network or manifest errors are non-fatal; the installed
// Studio continues with its current version.
bool TryRunStudioUpdate(int argc, char** argv);
