#pragma once

#include <cstddef>
#include <filesystem>

enum class FilesResult {
    kWritten,
    kExists, // Not written: the path was taken and replacing was not allowed.
    kFailed,
};

// Writes data to path so that it is on the disk once kWritten returns: a temporary file of its own
// beside path, created exclusively, written, flushed and closed, then moved onto path with the
// system's write-through or followed by a sync of the directory. Before that move path keeps what
// it held. Without replace, a path that exists gives kExists. A temporary file outlives the call
// only when the process dies during it; it is named <path>.<hex>.tmp.
FilesResult FilesWrite(const std::filesystem::path &path, const void *data, std::size_t size, bool replace);
