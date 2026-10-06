#include "files.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#endif

#include <cstdio>
#include <random>

namespace fs = std::filesystem;

namespace {

fs::path TempPath(const fs::path &path) {
    static std::mt19937_64 random{std::random_device{}()};
    char                   suffix[24];
    std::snprintf(suffix, sizeof(suffix), ".%016llx.tmp", static_cast<unsigned long long>(random()));
    fs::path temp = path;
    temp += suffix;
    return temp;
}

#ifdef _WIN32

using File = HANDLE;
const File kNoFile = INVALID_HANDLE_VALUE;

File CreateExclusive(const fs::path &path) {
    return CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
}

bool Taken() {
    return GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool WriteAll(File file, const char *data, std::size_t size) {
    while (size > 0) {
        DWORD chunk = size > 0x40000000 ? 0x40000000 : static_cast<DWORD>(size);
        DWORD written = 0;
        if (!WriteFile(file, data, chunk, &written, nullptr) || written == 0) {
            return false;
        }
        data += written;
        size -= written;
    }
    return true;
}

bool SyncAndClose(File file) {
    bool synced = FlushFileBuffers(file) != 0;
    return CloseHandle(file) != 0 && synced;
}

void Remove(const fs::path &path) {
    DeleteFileW(path.c_str());
}

FilesResult Commit(const fs::path &temp, const fs::path &path, bool replace) {
    DWORD flags = MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0);
    if (MoveFileExW(temp.c_str(), path.c_str(), flags)) {
        return FilesResult::kWritten;
    }
    return !replace && Taken() ? FilesResult::kExists : FilesResult::kFailed;
}

#else

using File = int;
const File kNoFile = -1;

File CreateExclusive(const fs::path &path) {
    return open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
}

bool Taken() {
    return errno == EEXIST;
}

bool WriteAll(File file, const char *data, std::size_t size) {
    while (size > 0) {
        ssize_t written = write(file, data, size);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return false;
        }
        data += written;
        size -= static_cast<std::size_t>(written);
    }
    return true;
}

bool Sync(File file) {
#ifdef __APPLE__
    // macOS's fsync stops at the drive's cache.
    if (fcntl(file, F_FULLFSYNC) == 0) {
        return true;
    }
#endif
    return fsync(file) == 0;
}

bool SyncAndClose(File file) {
    bool synced = Sync(file);
    return close(file) == 0 && synced;
}

void Remove(const fs::path &path) {
    unlink(path.c_str());
}

bool SyncDirectory(const fs::path &path) {
    File directory = open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    return directory != kNoFile && SyncAndClose(directory);
}

FilesResult Commit(const fs::path &temp, const fs::path &path, bool replace) {
    if (replace) {
        if (rename(temp.c_str(), path.c_str()) != 0) {
            return FilesResult::kFailed;
        }
    } else {
        // link fails rather than replace; the temporary name then goes.
        if (link(temp.c_str(), path.c_str()) != 0) {
            return Taken() ? FilesResult::kExists : FilesResult::kFailed;
        }
        Remove(temp);
    }
    return SyncDirectory(path) ? FilesResult::kWritten : FilesResult::kFailed;
}

#endif

} // namespace

FilesResult FilesWrite(const fs::path &path, const void *data, std::size_t size, bool replace) {
    fs::path temp;
    File     file = kNoFile;
    for (int attempt = 0; attempt < 8 && file == kNoFile; ++attempt) {
        temp = TempPath(path);
        file = CreateExclusive(temp);
        if (file == kNoFile && !Taken()) {
            return FilesResult::kFailed;
        }
    }
    if (file == kNoFile) {
        return FilesResult::kFailed;
    }
    bool written = WriteAll(file, static_cast<const char *>(data), size);
    if (!SyncAndClose(file) || !written) {
        Remove(temp);
        return FilesResult::kFailed;
    }
    FilesResult result = Commit(temp, path, replace);
    if (result != FilesResult::kWritten) {
        Remove(temp);
    }
    return result;
}
