#include "multi_instance.h"

#include "host_platform.h"
#include "host_window.h"
#include "multi_frame_share.h"
#include "presentation_layout.h"
#include "restart_launcher.h"
#include "runtime.h"

#if defined(GBAGAME_RECOMP_UI)
#include "recomp_runtime_ui.h"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

namespace gen3recomp {
namespace {

constexpr int kTileWidth = 240;
constexpr int kTileHeight = 160;

#if defined(GBAGAME_RECOMP_UI)
constexpr const char* kMultiUiScreens = "multitelas.screens";
constexpr const char* kMultiUiOpacity = "multitelas.overlay_opacity";
constexpr const char* kMultiUiReset = "multitelas.reset_all";
constexpr const char* kMultiUiLauncher = "multitelas.return_to_launcher";

struct MultiRuntimeUiContext {
    gbarecomp::HostWindow* window = nullptr;
    RecompRuntimeUi* ui = nullptr;
    int current_screens = 1;
    int requested_screens = 1;
    int overlay_opacity = 72;
    bool reset_requested = false;
    bool launcher_requested = false;
};

int multi_ui_get(void* opaque, const RecompRuntimeUiItem* item, int* out) {
    auto* context = static_cast<MultiRuntimeUiContext*>(opaque);
    if (!context || !context->window || !item || !item->key || !out) return 0;
    if (std::strcmp(item->key, kMultiUiScreens) == 0)
        *out = context->requested_screens;
    else if (std::strcmp(item->key, kMultiUiOpacity) == 0)
        *out = context->overlay_opacity;
    else if (std::strcmp(item->key, RECOMP_RUNTIME_UI_KEY_FULLSCREEN) == 0)
        *out = context->window->fullscreen();
    else if (std::strcmp(item->key, RECOMP_RUNTIME_UI_KEY_WINDOW_SCALE) == 0)
        *out = context->window->window_scale();
    else
        return 0;
    return 1;
}

int multi_ui_set(void* opaque, const RecompRuntimeUiItem* item, int value) {
    auto* context = static_cast<MultiRuntimeUiContext*>(opaque);
    if (!context || !context->window || !item || !item->key) return 0;
    if (std::strcmp(item->key, kMultiUiScreens) == 0) {
        context->requested_screens = std::clamp(value, 1, 12);
    } else if (std::strcmp(item->key, kMultiUiOpacity) == 0) {
        context->overlay_opacity = std::clamp(value, 0, 100);
    } else if (std::strcmp(item->key, RECOMP_RUNTIME_UI_KEY_FULLSCREEN) == 0) {
        context->window->set_fullscreen(value);
    } else if (std::strcmp(item->key, RECOMP_RUNTIME_UI_KEY_WINDOW_SCALE) == 0) {
        context->window->adjust_scale(
            value - context->window->window_scale());
    } else {
        return 0;
    }
    return 1;
}

int multi_ui_action(void* opaque, const RecompRuntimeUiItem* item) {
    auto* context = static_cast<MultiRuntimeUiContext*>(opaque);
    if (!context || !item || !item->key) return 0;
    if (std::strcmp(item->key, RECOMP_RUNTIME_UI_KEY_RESUME) == 0) {
        if (context->ui) recomp_runtime_ui_close(context->ui);
        return 1;
    }
    if (std::strcmp(item->key, kMultiUiReset) == 0) {
        context->reset_requested = true;
        if (context->ui) recomp_runtime_ui_close(context->ui);
        return 1;
    }
    if (std::strcmp(item->key, kMultiUiLauncher) == 0) {
        context->launcher_requested = true;
        if (context->ui) recomp_runtime_ui_close(context->ui);
        return 1;
    }
    return 0;
}

int multi_ui_enabled(void* opaque, const RecompRuntimeUiItem* item) {
    auto* context = static_cast<MultiRuntimeUiContext*>(opaque);
    if (!context || !context->window || !item || !item->key) return 1;
    if (std::strcmp(item->key, RECOMP_RUNTIME_UI_KEY_WINDOW_SCALE) == 0)
        return context->window->fullscreen() == 0;
    return 1;
}
#endif

void report_error(const std::string& message) {
    std::fprintf(stderr, "Full Emerald multitelas: %s\n", message.c_str());
#if defined(_WIN32)
    const char* no_dialog = std::getenv("GBARECOMP_MULTI_NO_DIALOG");
    if (!no_dialog || !*no_dialog || *no_dialog == '0') {
        MessageBoxA(nullptr, message.c_str(),
                    "Full Emerald - Multitelas", MB_OK | MB_ICONERROR);
    }
#endif
}

bool is_option_with_value(const std::string& value) {
    static constexpr const char* options[] = {
        "--save", "--save-path", "--tcp", "--frame-share", "--frames",
        "--steps", "--view-width", "--widescreen", "--mod-root"};
    return std::any_of(std::begin(options), std::end(options),
                       [&](const char* option) { return value == option; });
}

std::string option_value(const std::vector<std::string>& args,
                         const char* first, const char* second = nullptr) {
    for (std::size_t i = 1; i + 1 < args.size(); ++i) {
        if (args[i] == first || (second && args[i] == second))
            return args[i + 1];
    }
    return {};
}

std::vector<std::string> worker_base_args(
    const std::vector<std::string>& args) {
    std::vector<std::string> output;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& value = args[i];
        if (is_option_with_value(value)) {
            if (i + 1 < args.size()) ++i;
            continue;
        }
        if (value == "--window" || value == "--no-window" ||
            value == "--resize-view" || value == "--launcher" ||
            value == "--no-launcher" || value == "--fullscreen" ||
            value.rfind("--fullscreen=", 0) == 0) {
            continue;
        }
        output.push_back(value);
    }
    return output;
}

std::filesystem::path portable_multi_root(
    const std::vector<std::string>& args,
    const std::filesystem::path& executable) {
    // Prefer the selected ROM's directory. This keeps a portable copy working
    // from Desktop, another drive, or another PC without inheriting any path
    // from the developer machine.
    const std::string rom = option_value(args, "--rom");
    if (!rom.empty()) {
        std::error_code ec;
        const std::filesystem::path absolute_rom =
            std::filesystem::absolute(rom, ec).lexically_normal();
        if (!ec && absolute_rom.has_parent_path()) {
            return absolute_rom.parent_path() / "Full-Emerald-Multitelas";
        }
    }
#if defined(_WIN32)
    wchar_t local_app_data[32768]{};
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", local_app_data,
        static_cast<DWORD>(std::size(local_app_data)));
    if (length > 0 && length < std::size(local_app_data)) {
        return std::filesystem::path(local_app_data) / "Full Emerald" /
            "Multitelas";
    }
#endif
    return executable.parent_path() / "user-data" / "Multitelas";
}

void blend_pixel(std::uint8_t* pixel, std::uint8_t red,
                 std::uint8_t green, std::uint8_t blue, int alpha) {
    if (alpha <= 0) return;
    if (alpha > 255) alpha = 255;
    pixel[0] = static_cast<std::uint8_t>(
        (pixel[0] * (255 - alpha) + red * alpha + 127) / 255);
    pixel[1] = static_cast<std::uint8_t>(
        (pixel[1] * (255 - alpha) + green * alpha + 127) / 255);
    pixel[2] = static_cast<std::uint8_t>(
        (pixel[2] * (255 - alpha) + blue * alpha + 127) / 255);
}

void fill_rect(std::vector<std::uint8_t>& image, int width, int height,
               int x, int y, int w, int h, std::array<std::uint8_t, 3> color,
               int alpha) {
    const int left = std::clamp(x, 0, width);
    const int top = std::clamp(y, 0, height);
    const int right = std::clamp(x + w, 0, width);
    const int bottom = std::clamp(y + h, 0, height);
    for (int py = top; py < bottom; ++py) {
        for (int px = left; px < right; ++px) {
            auto* pixel = image.data() +
                (static_cast<std::size_t>(py) * width + px) * 3u;
            blend_pixel(pixel, color[0], color[1], color[2], alpha);
        }
    }
}

constexpr std::array<std::uint16_t, 10> kDigitBits = {
    0b111101101101111, 0b010110010010111, 0b111001111100111,
    0b111001111001111, 0b101101111001001, 0b111100111001111,
    0b111100111101111, 0b111001001001001, 0b111101111101111,
    0b111101111001111};

void draw_digit(std::vector<std::uint8_t>& image, int width, int height,
                int x, int y, int digit, int alpha) {
    if (digit < 0 || digit > 9) return;
    const std::uint16_t bits = kDigitBits[static_cast<std::size_t>(digit)];
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 3; ++column) {
            const int bit = 14 - (row * 3 + column);
            if ((bits & (1u << bit)) == 0) continue;
            fill_rect(image, width, height, x + column * 2, y + row * 2,
                      2, 2, {245, 251, 239}, alpha);
        }
    }
}

void draw_tile_overlay(std::vector<std::uint8_t>& image, int width,
                       int height, int tile_x, int tile_y, int number,
                       int opacity_percent) {
    const int alpha = std::clamp(opacity_percent, 0, 100) * 255 / 100;
    if (alpha == 0) return;
    const bool two_digits = number >= 10;
    const int box_width = two_digits ? 24 : 16;
    fill_rect(image, width, height, tile_x + 4, tile_y + 4,
              box_width, 16, {11, 50, 43}, alpha);
    fill_rect(image, width, height, tile_x + 4, tile_y + 4,
              box_width, 1, {113, 224, 178}, alpha);
    fill_rect(image, width, height, tile_x + 4, tile_y + 19,
              box_width, 1, {113, 224, 178}, alpha);
    if (two_digits) {
        draw_digit(image, width, height, tile_x + 8, tile_y + 7,
                   number / 10, alpha);
        draw_digit(image, width, height, tile_x + 16, tile_y + 7,
                   number % 10, alpha);
    } else {
        draw_digit(image, width, height, tile_x + 9, tile_y + 7,
                   number, alpha);
    }

    const int close_x = tile_x + kTileWidth - 20;
    const int close_y = tile_y + 4;
    fill_rect(image, width, height, close_x, close_y, 16, 16,
              {72, 26, 39}, alpha);
    for (int i = 4; i < 12; ++i) {
        fill_rect(image, width, height, close_x + i, close_y + i,
                  1, 1, {255, 226, 222}, alpha);
        fill_rect(image, width, height, close_x + 15 - i, close_y + i,
                  1, 1, {255, 226, 222}, alpha);
    }
}

bool write_bmp(const std::filesystem::path& path,
               const std::vector<std::uint8_t>& rgb, int width, int height) {
    const std::uint32_t row_bytes = static_cast<std::uint32_t>(width * 3);
    const std::uint32_t padding = (4u - (row_bytes & 3u)) & 3u;
    const std::uint32_t pixel_bytes = (row_bytes + padding) * height;
    const std::uint32_t file_bytes = 54u + pixel_bytes;
    std::array<std::uint8_t, 54> header{};
    header[0] = 'B'; header[1] = 'M';
    auto put32 = [&](int at, std::uint32_t value) {
        for (int i = 0; i < 4; ++i)
            header[at + i] = static_cast<std::uint8_t>(value >> (i * 8));
    };
    put32(2, file_bytes); put32(10, 54); put32(14, 40);
    put32(18, static_cast<std::uint32_t>(width));
    put32(22, static_cast<std::uint32_t>(height));
    header[26] = 1; header[28] = 24;
    put32(34, pixel_bytes);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(header.data()), header.size());
    const std::array<char, 3> zero{};
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* pixel = rgb.data() +
                (static_cast<std::size_t>(y) * width + x) * 3u;
            const char bgr[3] = {static_cast<char>(pixel[2]),
                                 static_cast<char>(pixel[1]),
                                 static_cast<char>(pixel[0])};
            file.write(bgr, sizeof(bgr));
        }
        file.write(zero.data(), padding);
    }
    return static_cast<bool>(file);
}

#if defined(_WIN32)

std::wstring current_executable(std::string* error) {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            if (error) *error = "GetModuleFileNameW failed";
            return {};
        }
        if (length < buffer.size())
            return std::wstring(buffer.data(), length);
        if (buffer.size() >= 32768) {
            if (error) *error = "executable path is too long";
            return {};
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring widen_argument(const std::string& value) {
    if (value.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                    value.c_str(), -1, nullptr, 0);
    UINT code_page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (count == 0) {
        code_page = CP_ACP;
        flags = 0;
        count = MultiByteToWideChar(code_page, flags, value.c_str(), -1,
                                    nullptr, 0);
    }
    if (count <= 0) return {};
    std::wstring output(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(code_page, flags, value.c_str(), -1,
                        output.data(), count);
    output.pop_back();
    return output;
}

std::string json_escape(const std::filesystem::path& path) {
    const std::string value = path.string();
    std::string output;
    output.reserve(value.size() + 8);
    for (char ch : value) {
        if (ch == '\\' || ch == '"') output.push_back('\\');
        output.push_back(ch);
    }
    return output;
}

class WinsockSession {
public:
    bool start(std::string* error) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            if (error) *error = "WSAStartup failed";
            return false;
        }
        active_ = true;
        return true;
    }
    ~WinsockSession() { if (active_) WSACleanup(); }
private:
    bool active_ = false;
};

class TcpClient {
public:
    ~TcpClient() { close(); }
    bool connect_local(int port, HANDLE process, std::string* error) {
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(90);
        while (std::chrono::steady_clock::now() < deadline) {
            if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
                if (error) *error = "worker exited before opening its port";
                return false;
            }
            SOCKET candidate = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (candidate != INVALID_SOCKET) {
                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_port = htons(static_cast<u_short>(port));
                address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                if (::connect(candidate,
                              reinterpret_cast<sockaddr*>(&address),
                              sizeof(address)) == 0) {
                    // A guest can busy-spin without ever returning to the TCP
                    // command loop. Never let one such process freeze the
                    // compositor (and therefore every healthy shiny-hunt run)
                    // forever. Ordinary IPC responses are measured in
                    // milliseconds; six seconds deliberately leaves generous
                    // room for a temporarily starved worker.
                    constexpr DWORD timeout_ms = 6000;
                    (void)setsockopt(
                        candidate, SOL_SOCKET, SO_RCVTIMEO,
                        reinterpret_cast<const char*>(&timeout_ms),
                        sizeof(timeout_ms));
                    (void)setsockopt(
                        candidate, SOL_SOCKET, SO_SNDTIMEO,
                        reinterpret_cast<const char*>(&timeout_ms),
                        sizeof(timeout_ms));
                    socket_ = candidate;
                    return true;
                }
                closesocket(candidate);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        if (error) *error = "timed out connecting to worker";
        return false;
    }
    bool send_line(const std::string& line, std::string* error) {
        if (socket_ == INVALID_SOCKET) return false;
        const std::string payload = line + "\n";
        std::size_t sent = 0;
        while (sent < payload.size()) {
            const int amount = send(
                socket_, payload.data() + sent,
                static_cast<int>(payload.size() - sent), 0);
            if (amount <= 0) {
                if (error) *error = "worker socket send failed";
                return false;
            }
            sent += static_cast<std::size_t>(amount);
        }
        return true;
    }
    bool receive_line(std::string* line, std::string* error) {
        if (!line || socket_ == INVALID_SOCKET) return false;
        for (;;) {
            if (pop_pending_line(line)) return true;
            char buffer[1024];
            const int amount = recv(socket_, buffer, sizeof(buffer), 0);
            if (amount <= 0) {
                if (error) {
                    const int code = amount == SOCKET_ERROR
                        ? WSAGetLastError() : 0;
                    *error = (code == WSAETIMEDOUT || code == WSAEWOULDBLOCK)
                        ? "worker response timed out"
                        : amount == 0 ? "worker closed its socket"
                                      : "worker socket receive failed (" +
                                            std::to_string(code) + ")";
                }
                return false;
            }
            pending_.append(buffer, static_cast<std::size_t>(amount));
            if (pending_.size() > 1024u * 1024u) {
                if (error) *error = "worker response exceeded limit";
                return false;
            }
        }
    }
    bool pop_pending_line(std::string* line) {
        if (!line) return false;
        const std::size_t newline = pending_.find('\n');
        if (newline == std::string::npos) return false;
        *line = pending_.substr(0, newline);
        pending_.erase(0, newline + 1);
        return true;
    }
    bool receive_ready(std::string* line, bool* complete, std::string* error) {
        if (!line || !complete || socket_ == INVALID_SOCKET) return false;
        *complete = pop_pending_line(line);
        if (*complete) return true;
        char buffer[1024];
        const int amount = recv(socket_, buffer, sizeof(buffer), 0);
        if (amount <= 0) {
            if (error) {
                const int code = amount == SOCKET_ERROR ? WSAGetLastError() : 0;
                *error = amount == 0 ? "worker closed its socket"
                    : "worker socket receive failed (" +
                          std::to_string(code) + ")";
            }
            return false;
        }
        pending_.append(buffer, static_cast<std::size_t>(amount));
        if (pending_.size() > 1024u * 1024u) {
            if (error) *error = "worker response exceeded limit";
            return false;
        }
        *complete = pop_pending_line(line);
        return true;
    }
    SOCKET native_socket() const noexcept { return socket_; }
    bool transact(const std::string& command, std::string* response,
                  std::string* error) {
        return send_line(command, error) && receive_line(response, error);
    }
    void close() noexcept {
        if (socket_ != INVALID_SOCKET) closesocket(socket_);
        socket_ = INVALID_SOCKET;
        pending_.clear();
    }
private:
    SOCKET socket_ = INVALID_SOCKET;
    std::string pending_;
};

struct Worker {
    int number = 0;
    int port = 0;
    std::filesystem::path save_path;
    std::filesystem::path state_root;
    std::filesystem::path reset_state_path;
    std::filesystem::path log_path;
    std::string share_name;
    gbarecomp::MultiFrameShare share;
    TcpClient client;
    PROCESS_INFORMATION process{};
    std::uint64_t last_guest_frame = 0;
    std::uint32_t run_seed = 0;
    bool responsive = true;
    bool has_frame = false;
    std::vector<std::uint8_t> frame =
        std::vector<std::uint8_t>(gbarecomp::MultiFrameShare::kMaxBytes);

    ~Worker() { shutdown(); }

    void shutdown() noexcept {
        if (process.hProcess) {
            if (responsive) {
                std::string ignored;
                std::string response;
                (void)client.transact("{\"quit\":true}", &response, &ignored);
            }
            client.close();
            const DWORD wait = responsive
                ? WaitForSingleObject(process.hProcess, 5000)
                : WAIT_TIMEOUT;
            if (wait == WAIT_TIMEOUT) {
                TerminateProcess(process.hProcess, 1);
                WaitForSingleObject(process.hProcess, 2000);
            }
            if (process.hThread) CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            process = {};
        }
    }
};

class WorkerJob {
public:
    WorkerJob() = default;
    WorkerJob(const WorkerJob&) = delete;
    WorkerJob& operator=(const WorkerJob&) = delete;
    ~WorkerJob() {
        if (handle_) CloseHandle(handle_);
    }

    bool open(std::string* error) {
        handle_ = CreateJobObjectW(nullptr, nullptr);
        if (!handle_) {
            if (error) *error = "could not create the worker containment job";
            return false;
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
            JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        limits.BasicLimitInformation.ActiveProcessLimit = 12;
        if (!SetInformationJobObject(handle_, JobObjectExtendedLimitInformation,
                                     &limits, sizeof(limits))) {
            if (error) {
                *error = "could not configure the worker containment job "
                         "(Windows error " +
                    std::to_string(GetLastError()) + ")";
            }
            CloseHandle(handle_);
            handle_ = nullptr;
            return false;
        }
        return true;
    }

    HANDLE get() const noexcept { return handle_; }

private:
    HANDLE handle_ = nullptr;
};

bool spawn_worker(Worker& worker, const std::wstring& executable,
                   const std::filesystem::path& working_directory,
                   const std::vector<std::string>& arguments,
                   HANDLE containment_job, std::string* error) {
    std::wstring command = quote_windows_argument(executable);
    for (const std::string& argument : arguments) {
        command.push_back(L' ');
        command += quote_windows_argument(widen_argument(argument));
    }
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE log = CreateFileW(
        worker.log_path.wstring().c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &security, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        if (error) *error = "could not create worker log";
        return false;
    }
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE) {
        CloseHandle(log);
        if (error) *error = "could not open the worker null input";
        return false;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = log;
    startup.hStdError = log;
    if (!CreateProcessW(executable.c_str(), mutable_command.data(), nullptr,
                        nullptr, TRUE,
                        CREATE_NO_WINDOW | ABOVE_NORMAL_PRIORITY_CLASS |
                            CREATE_SUSPENDED,
                        nullptr,
                        working_directory.wstring().c_str(), &startup,
                        &worker.process)) {
        CloseHandle(input);
        CloseHandle(log);
        if (error) {
            *error = "CreateProcessW failed for worker " +
                     std::to_string(worker.number) + " (Windows error " +
                     std::to_string(GetLastError()) + ")";
        }
        return false;
    }
    if (!containment_job ||
        !AssignProcessToJobObject(containment_job, worker.process.hProcess)) {
        const DWORD code = GetLastError();
        TerminateProcess(worker.process.hProcess, 1);
        WaitForSingleObject(worker.process.hProcess, 2000);
        CloseHandle(worker.process.hThread);
        CloseHandle(worker.process.hProcess);
        worker.process = {};
        CloseHandle(input);
        CloseHandle(log);
        if (error) {
            *error = "could not contain worker " +
                std::to_string(worker.number) + " (Windows error " +
                std::to_string(code) + ")";
        }
        return false;
    }
    if (ResumeThread(worker.process.hThread) == static_cast<DWORD>(-1)) {
        const DWORD code = GetLastError();
        TerminateProcess(worker.process.hProcess, 1);
        WaitForSingleObject(worker.process.hProcess, 2000);
        CloseHandle(worker.process.hThread);
        CloseHandle(worker.process.hProcess);
        worker.process = {};
        CloseHandle(input);
        CloseHandle(log);
        if (error) {
            *error = "could not start worker " +
                std::to_string(worker.number) + " (Windows error " +
                std::to_string(code) + ")";
        }
        return false;
    }
    CloseHandle(input);
    CloseHandle(log);
    return true;
}

std::string read_worker_log(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::string text((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
    constexpr std::size_t kTailBytes = 4096;
    if (text.size() > kTailBytes) text.erase(0, text.size() - kTailBytes);
    return text;
}

bool choose_free_ports(int count, std::vector<int>* output,
                       std::string* error) {
    if (!output || count < 1) return false;
    output->clear();
    const int first = 20000 + static_cast<int>(GetTickCount64() % 20000u);
    for (int offset = 0; offset < 20000 &&
                         static_cast<int>(output->size()) < count; ++offset) {
        const int port = 20000 + ((first - 20000 + offset) % 20000);
        SOCKET probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (probe == INVALID_SOCKET) continue;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<u_short>(port));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const bool available =
            bind(probe, reinterpret_cast<sockaddr*>(&address),
                 sizeof(address)) == 0;
        closesocket(probe);
        if (available) output->push_back(port);
    }
    if (static_cast<int>(output->size()) != count) {
        if (error) *error = "could not find enough free localhost ports";
        output->clear();
        return false;
    }
    return true;
}

bool response_ok(const std::string& response) {
    return response.find("\"ok\":true") != std::string::npos;
}

bool response_uint(const std::string& response, const char* field,
                   std::uint64_t* output) {
    if (!output) return false;
    const std::string token = std::string("\"") + field + "\":";
    const std::size_t start = response.find(token);
    if (start == std::string::npos) return false;
    std::size_t at = start + token.size();
    if (at >= response.size() || response[at] < '0' || response[at] > '9')
        return false;
    std::uint64_t value = 0;
    while (at < response.size() && response[at] >= '0' &&
           response[at] <= '9') {
        value = value * 10u + static_cast<unsigned>(response[at] - '0');
        ++at;
    }
    *output = value;
    return true;
}

bool broadcast_command(
    const std::vector<std::unique_ptr<Worker>>& workers,
    const std::vector<std::string>& commands, std::string* error) {
    if (workers.size() != commands.size()) return false;
    for (std::size_t i = 0; i < workers.size(); ++i) {
        if (!workers[i]->client.send_line(commands[i], error)) return false;
    }
    for (const auto& worker : workers) {
        std::string response;
        if (!worker->client.receive_line(&response, error)) return false;
        if (!response_ok(response)) {
            if (error) *error = "worker rejected command: " + response;
            return false;
        }
    }
    return true;
}

bool seed_workers(const std::vector<std::unique_ptr<Worker>>& workers,
                  std::uint32_t generation, std::string* error) {
    for (std::size_t index = 0; index < workers.size(); ++index) {
        Worker& worker = *workers[index];
        worker.run_seed = multi_instance_run_seed(
            generation, static_cast<std::uint32_t>(index + 1));
        const std::string command =
            "{\"set_run_seed\":true,\"value\":" +
            std::to_string(worker.run_seed) + "}";
        if (!worker.client.send_line(command, error)) return false;
    }
    for (const auto& worker : workers) {
        std::string response;
        std::uint64_t echoed = 0;
        if (!worker->client.receive_line(&response, error)) return false;
        if (!response_ok(response) ||
            !response_uint(response, "seed", &echoed) ||
            echoed != worker->run_seed) {
            if (error) {
                *error = "screen " + std::to_string(worker->number) +
                         " did not accept its independent RNG seed: " +
                         response;
            }
            return false;
        }
    }
    std::printf("multitelas_rng generation=%u unique=%zu\n",
                generation, workers.size());
    std::fflush(stdout);
    return true;
}

bool seed_one_worker(Worker& worker, std::uint32_t generation,
                     std::uint32_t seed_index, std::string* error) {
    worker.run_seed = multi_instance_run_seed(generation, seed_index);
    std::string response;
    const std::string command =
        "{\"set_run_seed\":true,\"value\":" +
        std::to_string(worker.run_seed) + "}";
    std::uint64_t echoed = 0;
    if (!worker.client.transact(command, &response, error) ||
        !response_ok(response) || !response_uint(response, "seed", &echoed) ||
        echoed != worker.run_seed) {
        if (error) {
            *error = "screen " + std::to_string(worker.number) +
                     " did not accept its independent RNG seed: " + response;
        }
        return false;
    }
    return true;
}

bool save_reset_baselines(
    const std::vector<std::unique_ptr<Worker>>& workers, std::string* error) {
    std::vector<std::string> commands;
    commands.reserve(workers.size());
    for (const auto& worker : workers) {
        commands.push_back(
            "{\"savestate_save\":true,\"path\":\"" +
            json_escape(worker->reset_state_path) + "\"}");
    }
    return broadcast_command(workers, commands, error);
}

bool reset_workers(const std::vector<std::unique_ptr<Worker>>& workers,
                   std::uint32_t* generation, std::string* error) {
    if (!generation) return false;
    std::vector<std::string> commands;
    commands.reserve(workers.size());
    for (const auto& worker : workers) {
        commands.push_back(
            "{\"savestate_load\":true,\"path\":\"" +
            json_escape(worker->reset_state_path) + "\"}");
    }
    if (!broadcast_command(workers, commands, error)) return false;
    ++*generation;
    return seed_workers(workers, *generation, error);
}

struct WorkerStepFailure {
    std::size_t index = 0;
    std::string reason;
};

struct WorkerStepResult {
    std::vector<WorkerStepFailure> failures;
};

WorkerStepResult step_workers(
    const std::vector<std::unique_ptr<Worker>>& workers,
    int frames, std::uint16_t keyinput) {
    WorkerStepResult result;
    const std::string command =
        "{\"run_frames\":true,\"n\":" + std::to_string(frames) +
        ",\"keyinput\":" + std::to_string(keyinput) + "}";
    std::vector<bool> awaiting(workers.size(), false);
    auto fail = [&](std::size_t index, std::string reason) {
        if (!awaiting[index] &&
            std::any_of(result.failures.begin(), result.failures.end(),
                        [&](const WorkerStepFailure& failure) {
                            return failure.index == index;
                        }))
            return;
        awaiting[index] = false;
        workers[index]->responsive = false;
        workers[index]->client.close();
        result.failures.push_back({index, std::move(reason)});
    };
    for (std::size_t index = 0; index < workers.size(); ++index) {
        std::string send_error;
        if (!workers[index]->client.send_line(command, &send_error)) {
            fail(index, std::move(send_error));
        } else {
            awaiting[index] = true;
        }
    }

    // Wait for every socket concurrently. Sequential blocking recv() made N
    // stalled workers cost N timeouts and let the first one freeze the whole
    // window. select() gives the entire batch one shared recovery deadline.
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(6);
    std::size_t remaining = static_cast<std::size_t>(
        std::count(awaiting.begin(), awaiting.end(), true));
    while (remaining > 0) {
        for (std::size_t index = 0; index < workers.size(); ++index) {
            if (!awaiting[index]) continue;
            std::string response;
            if (!workers[index]->client.pop_pending_line(&response)) continue;
            std::uint64_t frame = 0;
            if (!response_ok(response) ||
                !response_uint(response, "frame", &frame)) {
                fail(index, "invalid frame response: " + response);
            } else {
                awaiting[index] = false;
                workers[index]->last_guest_frame = frame;
            }
            --remaining;
        }
        if (remaining == 0) break;

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            for (std::size_t index = 0; index < workers.size(); ++index) {
                if (awaiting[index]) {
                    fail(index, "worker response exceeded the shared 6-second deadline");
                    --remaining;
                }
            }
            break;
        }
        fd_set readable;
        FD_ZERO(&readable);
        for (std::size_t index = 0; index < workers.size(); ++index)
            if (awaiting[index])
                FD_SET(workers[index]->client.native_socket(), &readable);
        const auto remaining_us =
            std::chrono::duration_cast<std::chrono::microseconds>(deadline - now);
        timeval timeout{};
        timeout.tv_sec = static_cast<long>(remaining_us.count() / 1000000);
        timeout.tv_usec = static_cast<long>(remaining_us.count() % 1000000);
        const int ready = select(0, &readable, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR) {
            const int code = WSAGetLastError();
            for (std::size_t index = 0; index < workers.size(); ++index) {
                if (awaiting[index]) {
                    fail(index, "worker socket wait failed (" +
                                    std::to_string(code) + ")");
                    --remaining;
                }
            }
            break;
        }
        if (ready == 0) continue;
        for (std::size_t index = 0; index < workers.size(); ++index) {
            if (!awaiting[index] ||
                !FD_ISSET(workers[index]->client.native_socket(), &readable))
                continue;
            std::string response;
            std::string receive_error;
            bool complete = false;
            if (!workers[index]->client.receive_ready(
                    &response, &complete, &receive_error)) {
                fail(index, std::move(receive_error));
                --remaining;
            } else if (complete) {
                std::uint64_t frame = 0;
                if (!response_ok(response) ||
                    !response_uint(response, "frame", &frame)) {
                    fail(index, "invalid frame response: " + response);
                } else {
                    awaiting[index] = false;
                    workers[index]->last_guest_frame = frame;
                }
                --remaining;
            }
        }
    }
    // Absolute frame counters are intentionally not compared. The workers all
    // receive the same frame budget and input, but independent seeds, menus,
    // encounters and shiny animations are allowed to diverge normally.
    return result;
}

#endif  // _WIN32

}  // namespace

int run_multi_instance(const std::vector<std::string>& runtime_args,
                       const MultiInstanceRequest& request) {
#if !defined(_WIN32)
    (void)runtime_args;
    (void)request;
    report_error("multitelas is currently implemented for Windows only");
    return 1;
#else
    if (request.count < 2 || request.count > 12) {
        report_error("multitelas requires 2 to 12 instances");
        return 1;
    }
    std::error_code ec;
    std::string error;
    std::filesystem::path requested_root = request.save_root;
    const std::wstring executable = current_executable(&error);
    if (executable.empty()) {
        report_error(error);
        return 1;
    }
    if (requested_root.empty()) {
        requested_root = portable_multi_root(
            runtime_args, std::filesystem::path(executable));
    }
    const std::filesystem::path save_root =
        std::filesystem::absolute(requested_root, ec).lexically_normal();
    if (ec || save_root.empty()) {
        report_error("could not resolve a writable multitelas data folder");
        return 1;
    }
    const std::filesystem::path saves = save_root / "saves";
    const std::filesystem::path states = save_root / "states";
    const std::filesystem::path work = save_root / "workers";
    std::filesystem::create_directories(saves, ec);
    std::filesystem::create_directories(states, ec);
    std::filesystem::create_directories(work, ec);
    if (ec) {
        report_error("could not create the multitelas data directories at " +
                     save_root.string());
        return 1;
    }
    WinsockSession winsock;
    if (!winsock.start(&error)) {
        report_error(error);
        return 1;
    }
    WorkerJob worker_job;
    if (!worker_job.open(&error)) {
        report_error(error);
        return 1;
    }

    const MultiInstanceStandardStorage standard_storage =
        multi_instance_standard_storage(
            runtime_args, std::filesystem::path(executable));
    std::vector<std::string> base_arguments = worker_base_args(runtime_args);
    const std::filesystem::path source_mod_root =
        std::filesystem::path(executable).parent_path() / "mods";
    const DWORD process_id = GetCurrentProcessId();
    std::vector<std::unique_ptr<Worker>> workers;
    workers.reserve(12);
    auto launch_worker = [&](int worker_number, int port) -> bool {
        auto worker = std::make_unique<Worker>();
        worker->number = worker_number;
        worker->port = port;
        char slot[16];
        std::snprintf(slot, sizeof(slot), "%02d", worker->number);
        worker->save_path = saves / (std::string("Emerald-") + slot + ".sav");
        worker->state_root = states / (std::string("Emerald-") + slot);
        worker->reset_state_path =
            worker->state_root / "multitelas-reset.gbas";
        std::filesystem::create_directories(worker->state_root, ec);
        const std::filesystem::path worker_cwd = work / slot;
        std::filesystem::create_directories(worker_cwd, ec);
        if (ec) {
            error = "could not create a worker directory on D:";
            return false;
        }
        worker->log_path = worker_cwd / "worker.log";
        const std::filesystem::path worker_mod_root = worker_cwd / "mods";
        if (std::filesystem::is_directory(source_mod_root)) {
            std::filesystem::create_directories(worker_mod_root, ec);
            std::filesystem::copy(
                source_mod_root, worker_mod_root,
                std::filesystem::copy_options::recursive |
                    std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                error = "could not prepare an isolated mod state for screen " +
                        std::to_string(worker->number);
                return false;
            }
        }
        if (!multi_instance_seed_worker_storage(
                standard_storage, worker->save_path, worker->state_root,
                &error)) {
            error = "could not synchronize screen " +
                std::to_string(worker->number) + " with the normal game: " +
                error;
            return false;
        }
        worker->share_name = "Local\\FullEmeraldMulti-" +
            std::to_string(process_id) + "-" + slot;
        if (!worker->share.create(worker->share_name, &error)) {
            return false;
        }
        std::vector<std::string> arguments = base_arguments;
        arguments.insert(arguments.end(), {
            "--multi-worker", "--tcp", std::to_string(worker->port),
            "--frame-share", worker->share_name,
            "--mod-root", worker_mod_root.string(),
            "--no-window", "--view-width", "240",
            "--save", worker->save_path.string(), "--quiet"});
        if (!spawn_worker(*worker, executable, worker_cwd, arguments,
                          worker_job.get(), &error)) {
            return false;
        }
        workers.push_back(std::move(worker));
        return true;
    };
    auto connect_worker = [&](Worker& worker) -> bool {
        if (!worker.client.connect_local(worker.port,
                                         worker.process.hProcess, &error)) {
            const std::string log = read_worker_log(worker.log_path);
            error = "screen " + std::to_string(worker.number) + ": " +
                    error + (log.empty() ? std::string{} : "\n\n" + log);
            return false;
        }
        std::string response;
        if (!worker.client.transact("{\"ping\":true}", &response, &error) ||
            !response_ok(response)) {
            error = "screen " + std::to_string(worker.number) +
                    " did not answer its handshake";
            return false;
        }
        return true;
    };

    std::vector<int> ports;
    if (!choose_free_ports(request.count, &ports, &error)) {
        report_error(error);
        return 1;
    }
    int next_worker_number = 1;
    for (int index = 0; index < request.count; ++index) {
        if (!launch_worker(next_worker_number++,
                           ports[static_cast<std::size_t>(index)])) {
            report_error(error);
            return 1;
        }
    }
    std::vector<std::size_t> startup_failures;
    for (std::size_t index = 0; index < workers.size(); ++index) {
        if (!connect_worker(*workers[index])) {
            std::fprintf(stderr,
                         "multitelas_startup_retry screen=%d reason=%s\n",
                         workers[index]->number, error.c_str());
            workers[index]->responsive = false;
            startup_failures.push_back(index);
        }
    }
    for (auto it = startup_failures.rbegin();
         it != startup_failures.rend(); ++it) {
        workers.erase(workers.begin() + static_cast<std::ptrdiff_t>(*it));
    }
    const int startup_target = request.count;
    int startup_attempts = 0;
    const int startup_attempt_limit = std::max(3, request.count * 2);
    while (static_cast<int>(workers.size()) < startup_target &&
           startup_attempts < startup_attempt_limit) {
        ++startup_attempts;
        std::vector<int> retry_ports;
        if (!choose_free_ports(1, &retry_ports, &error)) break;
        const int replacement_number = next_worker_number++;
        if (!launch_worker(replacement_number, retry_ports.front())) {
            std::fprintf(stderr,
                         "multitelas_startup_retry attempt=%d launch=%s\n",
                         startup_attempts, error.c_str());
            continue;
        }
        if (!connect_worker(*workers.back())) {
            std::fprintf(stderr,
                         "multitelas_startup_retry attempt=%d worker=%d reason=%s\n",
                         startup_attempts, replacement_number, error.c_str());
            workers.back()->responsive = false;
            workers.pop_back();
            continue;
        }
        std::fprintf(stderr,
                     "multitelas_startup_recovered worker=%d healthy=%zu\n",
                     replacement_number, workers.size());
    }
    if (workers.empty()) {
        report_error("no Multitelas worker could finish startup: " + error);
        return 1;
    }
    if (static_cast<int>(workers.size()) < startup_target) {
        std::fprintf(stderr,
                     "multitelas_startup_partial requested=%d healthy=%zu\n",
                     startup_target, workers.size());
    }

    if (!request.join_state.empty()) {
        const std::filesystem::path join_state =
            std::filesystem::absolute(request.join_state, ec).lexically_normal();
        if (ec || !std::filesystem::is_regular_file(join_state)) {
            report_error("the live Multitelas handoff state is missing");
            return 1;
        }
        std::vector<std::string> commands;
        commands.reserve(workers.size());
        for (std::size_t index = 0; index < workers.size(); ++index) {
            commands.push_back(
                "{\"savestate_load\":true,\"path\":\"" +
                json_escape(join_state) + "\"}");
        }
        if (!broadcast_command(workers, commands, &error)) {
            report_error("could not continue the live game in Multitelas: " +
                         error);
            return 1;
        }
    }

    LARGE_INTEGER performance_counter{};
    QueryPerformanceCounter(&performance_counter);
    std::uint32_t run_generation = static_cast<std::uint32_t>(
        performance_counter.QuadPart ^ GetTickCount64() ^ process_id);
    if (!save_reset_baselines(workers, &error)) {
        report_error("could not capture the synchronized reset baseline: " +
                     error);
        return 1;
    }
    if (!seed_workers(workers, run_generation, &error)) {
        report_error(error);
        return 1;
    }

    MultiInstanceGrid grid = multi_instance_grid(request.count);
    int surface_width = grid.columns * kTileWidth;
    int surface_height = grid.rows * kTileHeight;
    gbarecomp::HostWindow window;
    if (!window.open(1, surface_width, surface_height,
                     "Full Emerald - Multitelas", "raw", false,
                     false, false, true, 1100, 880)) {
        report_error("could not open the multitelas compositor window");
        return 1;
    }
    // Start a composite hunt at the monitor work area instead of the old 1x
    // logical size. It remains a normal resizable window, not fullscreen.
    window.maximize();
    const std::filesystem::path executable_dir =
        std::filesystem::path(executable).parent_path();
    window.load_input_config(executable_dir.string().c_str());
    const std::string controller_guid =
        option_value(runtime_args, "--controller-guid");
    int deadzone = 50;
    const std::string deadzone_value =
        option_value(runtime_args, "--controller-deadzone");
    if (!deadzone_value.empty()) {
        try { deadzone = std::clamp(std::stoi(deadzone_value), 0, 100); }
        catch (...) { deadzone = 50; }
    }
    window.configure_gamepad(controller_guid.c_str(), deadzone);
    int live_overlay_opacity = request.overlay_opacity;
#if defined(GBAGAME_RECOMP_UI)
    MultiRuntimeUiContext multi_ui_context{};
    multi_ui_context.window = &window;
    multi_ui_context.current_screens = static_cast<int>(workers.size());
    multi_ui_context.requested_screens = multi_ui_context.current_screens;
    multi_ui_context.overlay_opacity = live_overlay_opacity;
    const std::array<RecompRuntimeUiItem, 4> multi_ui_items{{
        {
            .key = kMultiUiScreens,
            .section = "Multitelas",
            .label = "Active screens",
            .description =
                "Add or remove synchronized shiny-hunt runs without "
                "restarting the screens already open.",
            .type = RECOMP_RUNTIME_UI_INT,
            .minimum = 1,
            .maximum = 12,
            .step = 1,
        },
        {
            .key = kMultiUiOpacity,
            .section = "Multitelas",
            .label = "Screen label opacity",
            .description =
                "Adjust the number and close-button visibility on each run.",
            .type = RECOMP_RUNTIME_UI_INT,
            .minimum = 0,
            .maximum = 100,
            .step = 4,
        },
        {
            .key = kMultiUiReset,
            .section = "Multitelas",
            .label = "Reset all runs",
            .description =
                "Reset every active run together and generate fresh RNG seeds.",
            .type = RECOMP_RUNTIME_UI_ACTION,
        },
        {
            .key = kMultiUiLauncher,
            .section = "Game",
            .label = "Return to launcher",
            .description =
                "Synchronize screen 1, close Multitelas safely, and open "
                "the launcher settings.",
            .type = RECOMP_RUNTIME_UI_ACTION,
        },
    }};
    RecompRuntimeUiStandardConfig multi_ui_config{};
    multi_ui_config.menu.title = "Pokemon Emerald";
    multi_ui_config.menu.subtitle = "Multitelas shiny hunt";
    multi_ui_config.menu.theme = "emerald";
    multi_ui_config.menu.callbacks.context = &multi_ui_context;
    multi_ui_config.menu.callbacks.get_value = &multi_ui_get;
    multi_ui_config.menu.callbacks.set_value = &multi_ui_set;
    multi_ui_config.menu.callbacks.run_action = &multi_ui_action;
    multi_ui_config.menu.callbacks.is_enabled = &multi_ui_enabled;
    multi_ui_config.features =
        RECOMP_RUNTIME_UI_STANDARD_FULLSCREEN |
        RECOMP_RUNTIME_UI_STANDARD_WINDOW_SCALE;
    multi_ui_config.window_scale_max = 8;
    multi_ui_config.extra_items = multi_ui_items.data();
    multi_ui_config.extra_item_count = multi_ui_items.size();
    multi_ui_context.ui =
        recomp_runtime_ui_create_standard(&multi_ui_config);
    if (multi_ui_context.ui)
        window.set_runtime_ui(multi_ui_context.ui);
#endif
    window.set_audio_enabled(false);
    // Workers already form the emulation clock. Do not put monitor vsync in
    // series with their barrier: on a 180 Hz display that added ~5.5 ms after
    // every twelve-worker step and made the generic late-frame pacer schedule
    // another full period. The compositor uses one absolute GBA-rate deadline
    // below, accounting for all worker and presentation time together.
    window.set_fast_forward_active(true);

    std::vector<std::uint8_t> composite(
        static_cast<std::size_t>(surface_width) * surface_height * 3u, 0);
    gbarecomp::FramePacer timer_resolution_scope;
    timer_resolution_scope.set_uncapped(true);
    // On machines with fewer than twelve logical CPUs, a one-frame IPC and
    // upload barrier for every GBA frame can make all twelve simulations run
    // below native speed. At 9..12 screens calculate two real guest frames per
    // barrier and present the newest one at ~30 Hz. Game logic remains close
    // to the native 59.7275 Hz for every worker; only the tiny tiled preview is
    // frame-skipped, which recovers the CPU/IPC budget without desynchronizing.
    int base_guest_frames = request.count >= 9 ? 2 : 1;
    const auto guest_frame_period = std::chrono::nanoseconds(
        static_cast<long long>(
            1e9 / gbarecomp::FramePacer::kGbaFrameHz + 0.5));
    auto frame_period = guest_frame_period * base_guest_frames;
    auto next_present = std::chrono::steady_clock::now() + frame_period;
    auto pace_compositor = [&]() {
        auto now = std::chrono::steady_clock::now();
        constexpr auto spin_tail = std::chrono::microseconds(500);
        if (now + spin_tail < next_present)
            std::this_thread::sleep_until(next_present - spin_tail);
        while (std::chrono::steady_clock::now() < next_present)
            std::this_thread::yield();
        now = std::chrono::steady_clock::now();
        if (now > next_present + frame_period)
            next_present = now + frame_period;
        else
            next_present += frame_period;
    };
    auto update_layout = [&]() -> bool {
        const MultiInstanceGrid updated =
            multi_instance_grid(static_cast<int>(workers.size()));
        const int updated_width = updated.columns * kTileWidth;
        const int updated_height = updated.rows * kTileHeight;
        if ((updated_width != surface_width ||
             updated_height != surface_height) &&
            !window.set_surface_size(updated_width, updated_height)) {
            error = "could not resize the multitelas compositor";
            return false;
        }
        grid = updated;
        surface_width = updated_width;
        surface_height = updated_height;
        composite.assign(static_cast<std::size_t>(surface_width) *
                             surface_height * 3u,
                         0);
        base_guest_frames = workers.size() >= 9 ? 2 : 1;
        frame_period = guest_frame_period * base_guest_frames;
        next_present = std::chrono::steady_clock::now() + frame_period;
        return true;
    };
    auto add_worker = [&]() -> bool {
        if (workers.size() >= 12) return true;

        // Clone the currently displayed point, not merely the executable's
        // boot point. The new run joins the same controls immediately, then
        // receives an independent RNG seed.
        const std::filesystem::path join_state =
            states / "multitelas-join-template.gbas";
        std::string response;
        const std::string save_command =
            "{\"savestate_save\":true,\"path\":\"" +
            json_escape(join_state) + "\"}";
        if (!workers.front()->client.transact(save_command, &response, &error) ||
            !response_ok(response)) {
            error = "could not capture the current screen for a new run";
            return false;
        }

        std::vector<int> dynamic_ports;
        if (!choose_free_ports(1, &dynamic_ports, &error)) return false;
        const int new_number = next_worker_number++;
        if (!launch_worker(new_number, dynamic_ports.front())) return false;
        auto discard_added_worker = [&]() {
            if (workers.empty()) return;
            workers.back()->responsive = false;
            workers.pop_back();
        };
        Worker& added = *workers.back();
        if (!connect_worker(added)) {
            discard_added_worker();
            return false;
        }

        ec.clear();
        std::filesystem::copy_file(
            workers.front()->reset_state_path, added.reset_state_path,
            std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "could not copy the collective reset baseline";
            discard_added_worker();
            return false;
        }
        const std::string load_command =
            "{\"savestate_load\":true,\"path\":\"" +
            json_escape(join_state) + "\"}";
        if (!added.client.transact(load_command, &response, &error) ||
            !response_ok(response)) {
            error = "new screen could not join the current game point";
            discard_added_worker();
            return false;
        }
        if (!seed_one_worker(added, run_generation,
                             static_cast<std::uint32_t>(new_number), &error)) {
            discard_added_worker();
            return false;
        }
        std::printf("multitelas_add display=%zu worker=%d seed=%u\n",
                    workers.size(), new_number, added.run_seed);
        std::fflush(stdout);
        return update_layout();
    };
    auto remove_worker = [&](std::size_t index) -> bool {
        if (workers.size() <= 1 || index >= workers.size()) return true;
        const int removed_number = workers[index]->number;
        workers.erase(workers.begin() + static_cast<std::ptrdiff_t>(index));
        std::printf("multitelas_remove worker=%d remaining=%zu\n",
                    removed_number, workers.size());
        std::fflush(stdout);
        return update_layout();
    };
    auto recover_failed_workers =
        [&](const WorkerStepResult& step_result) -> bool {
        if (step_result.failures.empty()) return true;
        const std::size_t desired_count = workers.size();
        for (const WorkerStepFailure& failure : step_result.failures) {
            const int number = failure.index < workers.size()
                ? workers[failure.index]->number : -1;
            std::fprintf(stderr,
                         "multitelas_recovery screen=%d reason=%s\n",
                         number, failure.reason.c_str());
        }
        std::vector<std::size_t> failed_indices;
        failed_indices.reserve(step_result.failures.size());
        for (const WorkerStepFailure& failure : step_result.failures)
            failed_indices.push_back(failure.index);
        std::sort(failed_indices.begin(), failed_indices.end());
        failed_indices.erase(
            std::unique(failed_indices.begin(), failed_indices.end()),
            failed_indices.end());
        for (auto it = failed_indices.rbegin(); it != failed_indices.rend(); ++it) {
            if (*it < workers.size() && !remove_worker(*it)) return false;
        }
        if (workers.empty()) {
            error = "every Multitelas worker stopped responding";
            return false;
        }

        // Preserve every healthy run. Replace only failed screens by cloning
        // the current point of the new display 1; each replacement then gets a
        // fresh independent RNG seed. If replacement itself is unavailable,
        // continue with the survivors instead of throwing away the session.
        while (workers.size() < desired_count) {
            const std::string previous_error = error;
            if (!add_worker()) {
                std::fprintf(stderr,
                             "multitelas_recovery replacement skipped: %s\n",
                             error.c_str());
                error = previous_error;
                break;
            }
        }
#if defined(GBAGAME_RECOMP_UI)
        multi_ui_context.current_screens = static_cast<int>(workers.size());
        multi_ui_context.requested_screens = multi_ui_context.current_screens;
#endif
        std::fprintf(stderr,
                     "multitelas_recovery healthy=%zu restored_target=%zu\n",
                     workers.size(), desired_count);
        std::fflush(stderr);
        return true;
    };
    bool paused = false;
    bool soft_reset_combo_held = false;
    bool running = true;
    bool return_to_launcher = false;
    int presented = 0;
    std::uint64_t guest_frames_advanced = 0;
    const auto play_started = std::chrono::steady_clock::now();
    while (running) {
        const gbarecomp::HostWindow::Events events = window.pump();
        if (events.quit) break;
#if defined(GBAGAME_RECOMP_UI)
        if (multi_ui_context.launcher_requested) {
            return_to_launcher = true;
            break;
        }
#endif
        if (events.toggle_fullscreen)
            window.set_fullscreen(window.fullscreen() ? 0 : 1);
        if (events.window_bigger) window.adjust_scale(1);
        if (events.window_smaller) window.adjust_scale(-1);
        if (events.toggle_fps) window.set_fps_readout(!window.fps_readout());
        if (events.toggle_pause) paused = !paused;

#if defined(GBAGAME_RECOMP_UI)
        // The Guide/Home menu changes a target count. Apply one transition at
        // this safe compositor boundary; existing workers remain alive and
        // keep their current RAM/save while a new worker joins from screen 1's
        // exact current savestate.
        multi_ui_context.current_screens =
            static_cast<int>(workers.size());
        live_overlay_opacity = multi_ui_context.overlay_opacity;
        if (running && multi_ui_context.requested_screens >
                           multi_ui_context.current_screens) {
            if (!add_worker()) {
                report_error("could not add a screen: " + error);
                running = false;
            }
        } else if (running && multi_ui_context.requested_screens <
                                  multi_ui_context.current_screens) {
            if (!remove_worker(workers.size() - 1)) {
                report_error(error);
                running = false;
            }
        }
        multi_ui_context.current_screens =
            static_cast<int>(workers.size());
#endif

        const std::size_t before_direct_screen_change = workers.size();
        std::size_t clicked_close = workers.size();
        if (events.mouse_left_pressed) {
            int drawable_width = 0;
            int drawable_height = 0;
            if (window.drawable_size(&drawable_width, &drawable_height)) {
                const gbarecomp::PresentationLayout layout =
                    gbarecomp::compute_presentation_layout(
                        drawable_width, drawable_height,
                        surface_width, surface_height);
                if (events.mouse_x >= layout.x && events.mouse_y >= layout.y &&
                    events.mouse_x < layout.x + layout.width &&
                    events.mouse_y < layout.y + layout.height) {
                    const int logical_x =
                        (events.mouse_x - layout.x) * surface_width /
                        std::max(layout.width, 1);
                    const int logical_y =
                        (events.mouse_y - layout.y) * surface_height /
                        std::max(layout.height, 1);
                    const int candidate = multi_instance_close_target(
                        logical_x, logical_y,
                        static_cast<int>(workers.size()));
                    if (candidate >= 0)
                        clicked_close = static_cast<std::size_t>(candidate);
                }
            }
        }
        const bool automated_remove = request.remove_after_present > 0 &&
            presented == request.remove_after_present;
        if (automated_remove && request.remove_display > 0 &&
            static_cast<std::size_t>(request.remove_display) <= workers.size()) {
            clicked_close = static_cast<std::size_t>(request.remove_display - 1);
        }
        if (clicked_close < workers.size()) {
            if (!remove_worker(clicked_close)) {
                report_error(error);
                running = false;
            }
        } else if ((events.remove_screen || automated_remove) &&
                   workers.size() > 1) {
            if (!remove_worker(workers.size() - 1)) {
                report_error(error);
                running = false;
            }
        }
        const bool automated_add = request.add_after_present > 0 &&
            presented == request.add_after_present;
        if (running && (events.add_screen || automated_add) &&
            workers.size() < 12) {
            if (!add_worker()) {
                report_error("could not add a screen: " + error);
                running = false;
            }
        }
#if defined(GBAGAME_RECOMP_UI)
        // Mouse X and rebindable Add/Remove hotkeys are peers of the menu. If
        // one of those paths changed the live set, make that count the menu's
        // new target instead of undoing it on the following frame.
        if (workers.size() != before_direct_screen_change)
            multi_ui_context.requested_screens =
                static_cast<int>(workers.size());
#endif

        const bool automated_reset = request.reset_after_present > 0 &&
            presented == request.reset_after_present;
        const bool soft_reset_combo_down =
            multi_instance_soft_reset_pressed(events.keyinput);
        const bool soft_reset_combo_edge =
            soft_reset_combo_down && !soft_reset_combo_held;
        soft_reset_combo_held = soft_reset_combo_down;
        const bool menu_reset =
#if defined(GBAGAME_RECOMP_UI)
            multi_ui_context.reset_requested;
#else
            false;
#endif
        if (running && (events.reset_console || automated_reset || menu_reset ||
                        soft_reset_combo_edge)) {
#if defined(GBAGAME_RECOMP_UI)
            multi_ui_context.reset_requested = false;
#endif
            if (!reset_workers(workers, &run_generation, &error)) {
                report_error("collective reset failed: " + error);
                running = false;
            } else {
                paused = false;
                guest_frames_advanced = 0;
                next_present = std::chrono::steady_clock::now() + frame_period;
            }
        }

        if (events.save_slot || events.load_slot) {
            std::vector<std::string> commands;
            commands.reserve(workers.size());
            for (const auto& worker : workers) {
                const int slot = events.save_slot ? events.save_slot
                                                  : events.load_slot;
                const std::filesystem::path state = worker->state_root /
                    ("slot-" + std::to_string(slot) + ".gbas");
                commands.push_back(
                    std::string("{\"") +
                    (events.save_slot ? "savestate_save" : "savestate_load") +
                    "\":true,\"path\":\"" + json_escape(state) + "\"}");
            }
            if (!broadcast_command(workers, commands, &error)) {
                report_error(error);
                running = false;
            }
        }
        if (!running) break;
        if (paused) {
            window.present(composite.data());
            pace_compositor();
            continue;
        }

        int frames = base_guest_frames;
        if (events.fast_forward) {
            const int multiplier = events.fast_forward_multiplier == 0
                ? gbarecomp::FramePacer::kMaxSpeedMultiplier
                : std::clamp(events.fast_forward_multiplier, 1,
                             gbarecomp::FramePacer::kMaxSpeedMultiplier);
            frames *= multiplier;
        }
        const WorkerStepResult step_result =
            step_workers(workers, frames, events.keyinput);
        if (!step_result.failures.empty()) {
            if (!recover_failed_workers(step_result)) {
                report_error(error);
                running = false;
                break;
            }
            next_present = std::chrono::steady_clock::now() + frame_period;
            continue;
        }
        guest_frames_advanced += static_cast<std::uint64_t>(frames);

        std::fill(composite.begin(), composite.end(), 0);
        for (std::size_t index = 0; index < workers.size(); ++index) {
            Worker& worker = *workers[index];
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            if (!worker.share.snapshot(worker.frame.data(), worker.frame.size(),
                                       &width, &height) ||
                width != kTileWidth || height != kTileHeight) {
                // Shared-memory publication is lock-free. A writer can be in
                // flight during this snapshot; keep the prior complete image
                // for one presentation instead of killing every run.
                if (!worker.has_frame) {
                    std::fill(worker.frame.begin(), worker.frame.end(), 0);
                }
                std::fprintf(stderr,
                             "multitelas_frame_retry screen=%d previous=%s\n",
                             worker.number, worker.has_frame ? "yes" : "no");
            } else {
                worker.has_frame = true;
            }
            const int tile_x = static_cast<int>(index % grid.columns) *
                               kTileWidth;
            const int tile_y = static_cast<int>(index / grid.columns) *
                               kTileHeight;
            for (int row = 0; row < kTileHeight; ++row) {
                const std::uint8_t* source = worker.frame.data() +
                    static_cast<std::size_t>(row) * kTileWidth * 3u;
                std::uint8_t* destination = composite.data() +
                    (static_cast<std::size_t>(tile_y + row) * surface_width +
                     tile_x) * 3u;
                std::memcpy(destination, source, kTileWidth * 3u);
            }
            draw_tile_overlay(composite, surface_width, surface_height,
                              tile_x, tile_y, static_cast<int>(index + 1),
                              live_overlay_opacity);
        }
        if (!running) break;
        window.present(composite.data());
        pace_compositor();
        ++presented;
        if (request.frame_limit > 0 && presented >= request.frame_limit)
            break;
    }
    const double play_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - play_started).count();
    if (presented > 0 && !workers.empty()) {
        std::printf("multitelas_lockstep screens=%zu presents=%d "
                    "guest_frame=%llu wall_seconds=%.3f present_hz=%.3f "
                    "guest_hz=%.3f\n",
                    workers.size(), presented,
                    static_cast<unsigned long long>(
                        workers.front()->last_guest_frame),
                    play_seconds, presented / play_seconds,
                    guest_frames_advanced / play_seconds);
        std::fflush(stdout);
    }
    if (!request.dump_bmp.empty()) {
        const std::filesystem::path output =
            std::filesystem::absolute(request.dump_bmp, ec).lexically_normal();
        if (ec || output.empty() ||
            !write_bmp(output, composite, surface_width, surface_height)) {
            report_error("could not write the multitelas smoke image");
            running = false;
        }
    }
#if defined(GBAGAME_RECOMP_UI)
    window.set_runtime_ui(nullptr);
    recomp_runtime_ui_destroy(multi_ui_context.ui);
    multi_ui_context.ui = nullptr;
#endif
    window.close();
    std::filesystem::path published_save;
    std::filesystem::path published_states;
    if (!workers.empty()) {
        // The visible ordering is authoritative: after closing an intermediate
        // screen the next survivor becomes display 1, and that is the session
        // which continues in ordinary single-screen mode.
        published_save = workers.front()->save_path;
        published_states = workers.front()->state_root;
    }
    workers.clear();  // sends quit; each worker flushes its own battery save
    if (!published_save.empty() &&
        !multi_instance_publish_worker_storage(
            standard_storage, published_save, published_states, &error)) {
        report_error("could not synchronize Multitelas back to the normal "
                     "game: " + error);
        running = false;
    }
    if (!running) return 1;
    return return_to_launcher ? gbarecomp::kExitRestartLauncher : 0;
#endif
}

}  // namespace gen3recomp
