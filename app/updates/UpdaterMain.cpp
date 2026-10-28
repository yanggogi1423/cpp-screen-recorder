// Standalone updater: no Qt DLL/framework remains locked while replacing the app.
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#else
#include <CommonCrypto/CommonDigest.h>
#include <libproc.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
namespace fs = std::filesystem;
namespace {
void ensure(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::string line(const fs::path &path) {
    std::ifstream file(path);
    std::string text;
    std::getline(file, text);
    if (!text.empty() && text.back() == '\r')
        text.pop_back();
    return text;
}
std::string utf8(const fs::path &path) {
    const auto value = path.generic_u8string();
    return std::string(value.begin(), value.end());
}
fs::path pathFrom(const std::string &path) {
    return fs::u8path(path);
}
std::string hashFile(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    ensure(bool(file), "Cannot read update archive");
    unsigned char digest[32]{};
    char bytes[65536];
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    ensure(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0,
           "SHA256 unavailable");
    DWORD objectSize = 0, count = 0;
    BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
                      sizeof(objectSize), &count, 0);
    std::vector<unsigned char> object(objectSize);
    ensure(BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) == 0,
           "SHA256 initialization failed");
    while (file.read(bytes, sizeof(bytes)) || file.gcount())
        ensure(BCryptHashData(hash, reinterpret_cast<PUCHAR>(bytes), ULONG(file.gcount()), 0) == 0,
               "SHA256 failed");
    ensure(!file.bad() && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0, "SHA256 failed");
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
#else
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    while (file.read(bytes, sizeof(bytes)) || file.gcount())
        CC_SHA256_Update(&context, bytes, CC_LONG(file.gcount()));
    ensure(!file.bad(), "Archive read failed");
    CC_SHA256_Final(digest, &context);
#endif
    std::ostringstream text;
    for (auto value : digest)
        text << std::hex << std::setfill('0') << std::setw(2) << unsigned(value);
    return text.str();
}
#ifdef _WIN32
std::wstring quote(const std::wstring &value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (auto c : value) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"')
            result.append(slashes * 2 + 1, L'\\');
        else
            result.append(slashes, L'\\');
        slashes = 0;
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
fs::path modulePath() {
    wchar_t name[32768];
    DWORD count = GetModuleFileNameW(nullptr, name, 32768);
    ensure(count > 0 && count < 32768, "Cannot locate updater");
    return fs::path(std::wstring(name, count));
}
int run(const fs::path &program, const std::vector<fs::path> &args, std::string *output = nullptr) {
    std::wstring command = quote(program.wstring());
    for (const auto &arg : args)
        command += L" " + quote(arg.wstring());
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    ensure(CreatePipe(&read, &write, &security, 0), "Cannot create extraction pipe");
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write;
    startup.hStdError = write;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(write);
    if (!created) {
        CloseHandle(read);
        throw std::runtime_error("Cannot start archive utility");
    }
    char bytes[4096];
    DWORD count = 0;
    std::string text;
    while (ReadFile(read, bytes, sizeof(bytes), &count, nullptr) && count) {
        text.append(bytes, count);
        if (text.size() > 16 * 1024 * 1024) {
            TerminateProcess(process.hProcess, 2);
            break;
        }
    }
    CloseHandle(read);
    WaitForSingleObject(process.hProcess, 60000);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (output)
        *output = std::move(text);
    return int(code);
}
fs::path tarPath() {
    wchar_t system[32768];
    ensure(GetSystemDirectoryW(system, 32768) > 0, "Cannot locate Windows directory");
    return fs::path(system) / L"tar.exe";
}
bool launch(const fs::path &file, bool check) {
    std::wstring command = quote(file.wstring());
    STARTUPINFOW start{};
    start.cb = sizeof(start);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(file.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        file.parent_path().c_str(), &start, &process))
        return false;
    bool ok = true;
    if (check) {
        WaitForInputIdle(process.hProcess, 5000);
        ok = WaitForSingleObject(process.hProcess, 5000) == WAIT_TIMEOUT;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return ok;
}
void waitForParent(unsigned long id, const fs::path &expected) {
    HANDLE parent = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
    if (!parent) {
        ensure(GetLastError() == ERROR_INVALID_PARAMETER, "Cannot inspect running app");
        return;
    }
    if (WaitForSingleObject(parent, 0) == WAIT_OBJECT_0) {
        CloseHandle(parent);
        return;
    }
    wchar_t path[32768];
    DWORD count = 32768;
    const bool same = QueryFullProcessImageNameW(parent, 0, path, &count) &&
                      fs::equivalent(fs::path(std::wstring(path, count)), expected);
    if (!same) {
        const bool exited = WaitForSingleObject(parent, 0) == WAIT_OBJECT_0;
        CloseHandle(parent);
        if (exited)
            return;
        throw std::runtime_error("The update process does not match this app");
    }
    DWORD result = WAIT_TIMEOUT;
    for (int i = 0; i < 600; ++i) {
        auto cancel = modulePath();
        cancel += ".cancel";
        if (fs::exists(cancel)) {
            CloseHandle(parent);
            throw std::runtime_error("Update canceled");
        }
        result = WaitForSingleObject(parent, 100);
        if (result != WAIT_TIMEOUT)
            break;
    }
    CloseHandle(parent);
    ensure(result == WAIT_OBJECT_0, "App is still running; no files were changed");
}
#else
fs::path modulePath() {
    char path[PROC_PIDPATHINFO_MAXSIZE];
    ensure(proc_pidpath(getpid(), path, sizeof(path)) > 0, "Cannot locate updater");
    return path;
}
fs::path tarPath() {
    return "/usr/bin/tar";
}
int run(const fs::path &program, const std::vector<fs::path> &args, std::string *output = nullptr) {
    int handles[2];
    ensure(pipe(handles) == 0, "Cannot create extraction pipe");
    const auto child = fork();
    ensure(child >= 0, "Cannot start archive utility");
    if (child == 0) {
        close(handles[0]);
        dup2(handles[1], STDOUT_FILENO);
        dup2(handles[1], STDERR_FILENO);
        close(handles[1]);
        std::vector<std::string> strings{program.string()};
        for (const auto &arg : args)
            strings.push_back(arg.string());
        std::vector<char *> argv;
        for (auto &value : strings)
            argv.push_back(value.data());
        argv.push_back(nullptr);
        execv(program.c_str(), argv.data());
        _exit(127);
    }
    close(handles[1]);
    std::string text;
    char bytes[4096];
    ssize_t count;
    while ((count = read(handles[0], bytes, sizeof(bytes))) > 0) {
        text.append(bytes, size_t(count));
        if (text.size() > 16 * 1024 * 1024) {
            kill(child, SIGKILL);
            break;
        }
    }
    close(handles[0]);
    int status = 0;
    waitpid(child, &status, 0);
    if (output)
        *output = std::move(text);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
bool launch(const fs::path &file, bool check) {
    auto child = fork();
    if (child < 0)
        return false;
    if (child == 0) {
        chdir(file.parent_path().c_str());
        execl(file.c_str(), file.c_str(), nullptr);
        _exit(127);
    }
    if (!check)
        return true;
    std::this_thread::sleep_for(std::chrono::seconds(5));
    int status = 0;
    return waitpid(child, &status, WNOHANG) == 0;
}
void waitForParent(unsigned long id, const fs::path &expected) {
    for (int i = 0; i < 600; ++i) {
        auto cancel = modulePath();
        cancel += ".cancel";
        ensure(!fs::exists(cancel), "Update canceled");
        if (kill(pid_t(id), 0) != 0) {
            ensure(errno == ESRCH, "Cannot inspect running app");
            return;
        }
        char path[PROC_PIDPATHINFO_MAXSIZE];
        if (proc_pidpath(int(id), path, sizeof(path)) <= 0) {
            if (kill(pid_t(id), 0) != 0 && errno == ESRCH)
                return;
            throw std::runtime_error("Cannot inspect running app");
        }
        ensure(fs::equivalent(path, expected), "The running app does not match this update");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    throw std::runtime_error("App is still running; no files were changed");
}
#endif
fs::path appExecutable(const fs::path &root) {
#ifdef _WIN32
    return root / "bin" / "FARecorder.exe";
#else
    return root / "Contents" / "MacOS" / "FARecorder";
#endif
}
const std::string packageRoot =
#ifdef _WIN32
    "FARecorder-Windows-x64";
#else
    "FARecorder.app";
#endif
bool safeRelative(std::string value) {
    if (value.empty() || value.front() == '/' || value.find('\\') != std::string::npos ||
        value.find(':') != std::string::npos || value.find('\r') != std::string::npos)
        return false;
    for (unsigned char c : value)
        if (c < 32 || c > 126)
            return false;
    const auto path = pathFrom(value);
    if (path.is_absolute())
        return false;
    for (const auto &part : path)
        if (part == ".." || part == "." ||
            (!part.empty() && (utf8(part).back() == '.' || utf8(part).back() == ' ')))
            return false;
    return true;
}
bool inside(const fs::path &child, const fs::path &parent) {
    const auto relative = child.lexically_normal().lexically_relative(parent.lexically_normal());
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
void validateArchive(const fs::path &archive) {
    std::string listing;
    ensure(run(tarPath(), {"-tf", archive}, &listing) == 0, "Cannot inspect update archive");
    std::istringstream entries(listing);
    std::string value;
    size_t count = 0;
    while (std::getline(entries, value)) {
        if (!value.empty() && value.back() == '\r')
            value.pop_back();
        ensure(++count < 100000 && safeRelative(value) &&
                   (value == packageRoot || value == packageRoot + "/" ||
                    value.rfind(packageRoot + "/", 0) == 0),
               "Unsafe path in update archive");
    }
    ensure(count > 0, "Empty update archive");
    std::string details;
    ensure(run(tarPath(), {"-tvf", archive}, &details) == 0, "Cannot inspect archive links");
    std::istringstream descriptions(details);
    std::set<std::string> links;
    while (std::getline(descriptions, value)) {
        if (!value.empty() && value.back() == '\r')
            value.pop_back();
        if (value.empty())
            continue;
        ensure(value.front() == '-' || value.front() == 'd' || value.front() == 'l',
               "Special file in update archive");
        if (value.front() == 'l') {
#ifdef _WIN32
            throw std::runtime_error("Windows update archives may not contain symlinks");
#endif
            const auto arrow = value.find(" -> ");
            const auto root = value.find(packageRoot + "/");
            ensure(arrow != std::string::npos && root != std::string::npos && root < arrow,
                   "Invalid update symlink");
            const auto name = value.substr(root, arrow - root), target = value.substr(arrow + 4);
            ensure(!pathFrom(target).is_absolute() && target.find(':') == std::string::npos &&
                       target.find('\\') == std::string::npos,
                   "External link in update archive");
            ensure(inside((pathFrom(name).parent_path() / pathFrom(target)).lexically_normal(),
                          pathFrom(packageRoot)),
                   "Escaping link in archive");
            auto lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            links.insert(lower);
        }
        ensure(value.find(" link to ") == std::string::npos, "Hard link in update archive");
    }
    std::istringstream paths(listing);
    while (std::getline(paths, value)) {
        if (!value.empty() && value.back() == '\r')
            value.pop_back();
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        for (const auto &link : links)
            ensure(value.rfind(link + "/", 0) != 0, "Archive writes through a symlink");
    }
}
void writeManifest(const fs::path &root) {
    ensure(line(root / "farecorder-package.txt") == "FARecorder" &&
               fs::is_regular_file(appExecutable(root)),
           "Not an FARecorder package");
    std::set<std::string> files;
    for (const auto &entry : fs::recursive_directory_iterator(root))
        if (!entry.is_directory() || entry.is_symlink())
            files.insert(utf8(entry.path().lexically_relative(root)));
    std::ofstream output(root / "update-files.txt", std::ios::binary | std::ios::trunc);
    ensure(bool(output), "Cannot write package manifest");
    files.insert("update-files.txt");
    for (const auto &file : files) {
        ensure(safeRelative(file), "Unsafe package file name");
        output << file << '\n';
    }
    ensure(bool(output), "Cannot finish package manifest");
}
void preserveUserFiles(const fs::path &oldRoot, const fs::path &newRoot) {
    std::ifstream file(oldRoot / "update-files.txt");
    ensure(bool(file), "Installed package has no update manifest; use manual update");
    std::set<std::string> known;
    std::string item;
    while (std::getline(file, item)) {
        if (!item.empty() && item.back() == '\r')
            item.pop_back();
        ensure(safeRelative(item), "Invalid installed manifest");
        known.insert(item);
    }
    ensure(known.count("farecorder-package.txt") && known.count("update-files.txt"),
           "Invalid installed manifest");
    for (const auto &entry : fs::recursive_directory_iterator(oldRoot)) {
        if (entry.is_directory() && !entry.is_symlink())
            continue;
        const auto relative = entry.path().lexically_relative(oldRoot);
        if (known.count(utf8(relative)))
            continue;
        const auto target = newRoot / relative;
        ensure(!fs::exists(target) && !fs::is_symlink(target),
               "A user file conflicts with the update; no files changed");
        fs::create_directories(target.parent_path());
        if (entry.is_symlink())
            fs::copy_symlink(entry.path(), target);
        else
            fs::copy_file(entry.path(), target);
    }
}
void apply(const fs::path &archive, const fs::path &target, const std::string &digest,
           const std::string &version, unsigned long parent) {
    const auto root = fs::canonical(target);
    const auto module = fs::canonical(modulePath());
    ensure(root.has_filename() && root != root.root_path() && !inside(module, root),
           "Updater must run outside the application folder");
    ensure(fs::canonical(archive).parent_path() == module.parent_path(),
           "Archive is not in this update's work folder");
    ensure(digest.size() == 64 && hashFile(archive) == digest,
           "Update checksum mismatch; no files changed");
    ensure(line(root / "farecorder-package.txt") == "FARecorder" &&
               fs::is_regular_file(appExecutable(root)),
           "Not an installed FARecorder package");
    validateArchive(archive);
    const auto token = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto stage = root.parent_path() / pathFrom(".FARecorder-update-" + token);
    ensure(fs::create_directory(stage), "Cannot create staging folder beside the app");
    ensure(run(tarPath(), {"-xf", archive, "-C", stage}) == 0,
           "Cannot extract update; original app preserved");
    const auto fresh = stage / pathFrom(packageRoot);
    ensure(line(fresh / "farecorder-package.txt") == "FARecorder" &&
               fs::is_regular_file(appExecutable(fresh)),
           "Invalid update package");
    {
        std::ifstream marker(fresh / "farecorder-package.txt");
        std::string app, actual;
        std::getline(marker, app);
        std::getline(marker, actual);
        if (!actual.empty() && actual.back() == '\r')
            actual.pop_back();
        ensure(actual == version, "Update package version mismatch");
    }
    ensure(fs::is_regular_file(fresh / "update-files.txt"),
           "Update is missing its package manifest");
    for (const auto &entry : fs::recursive_directory_iterator(fresh))
        if (entry.is_symlink())
            ensure(inside(fs::canonical(entry.path()), fs::canonical(fresh)),
                   "Update symlink escapes its package");
    auto ready = module;
    ready += ".ready";
    {
        std::ofstream notice(ready);
        notice << "ready\n";
        ensure(bool(notice), "Cannot signal update preparation");
    }
    auto canceled = module;
    canceled += ".cancel";
    ensure(!fs::exists(canceled), "Update canceled");
    waitForParent(parent, appExecutable(root));
    ensure(!fs::exists(canceled), "Update canceled");
    auto backup = root;
    backup += pathFrom(".backup-" + token);
    bool replaced = false, moved = false;
    try {
        preserveUserFiles(root, fresh);
        fs::rename(root, backup);
        moved = true;
        fs::rename(fresh, root);
        replaced = true;
        ensure(launch(appExecutable(root), true), "Updated app could not start");
    } catch (...) {
        if (replaced)
            fs::rename(root, stage / "failed-package");
        if (moved)
            fs::rename(backup, root);
        launch(appExecutable(root), false);
        throw;
    }
    // Keep the complete previous version for manual recovery. Never delete recordings.
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<fs::path> args;
    for (int i = 0; i < argc; ++i)
        args.emplace_back(argv[i]);
#else
int main(int argc, char **argv) {
    std::vector<fs::path> args;
    for (int i = 0; i < argc; ++i)
        args.emplace_back(argv[i]);
#endif
    bool quiet = false;
    if (argc > 1 && args.back() == "--quiet") {
        quiet = true;
        --argc;
        args.pop_back();
    }
    try {
        if (argc == 3 && args[1] == "--write-manifest") {
            writeManifest(fs::canonical(args[2]));
            return 0;
        }
        ensure(argc == 7 && args[1] == "--apply", "Invalid updater arguments");
        apply(args[2], args[3], utf8(args[4]), utf8(args[5]), std::stoul(utf8(args[6])));
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        try {
            auto path = modulePath();
            path += ".error";
            std::ofstream log(path);
            log << error.what() << '\n';
        } catch (...) {
        }
#ifdef _WIN32
        auto prepared = modulePath();
        prepared += ".ready";
        if (!quiet && fs::exists(prepared) && std::string(error.what()) != "Update canceled")
            MessageBoxA(nullptr, error.what(), "FARecorder update was not completed",
                        MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
}
