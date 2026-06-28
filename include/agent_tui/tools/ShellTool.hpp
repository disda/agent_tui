#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "agent_tui/tools/Tool.hpp"
#include "agent_tui/workspace/Workspace.hpp"

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace agent_tui {

namespace shell_tool_detail {

inline std::string get_arg(const JsonLike& arguments, const std::string& key, const std::string& fallback = {}) {
    const auto it = arguments.find(key);
    return it == arguments.end() ? fallback : it->second;
}

inline int get_int_arg(const JsonLike& arguments, const std::string& key, int fallback) {
    const auto it = arguments.find(key);
    if (it == arguments.end() || it->second.empty()) {
        return fallback;
    }
    try {
        return std::stoi(it->second);
    } catch (...) {
        return fallback;
    }
}

inline std::size_t get_size_arg(const JsonLike& arguments, const std::string& key, std::size_t fallback) {
    const auto it = arguments.find(key);
    if (it == arguments.end() || it->second.empty()) {
        return fallback;
    }
    try {
        return static_cast<std::size_t>(std::stoull(it->second));
    } catch (...) {
        return fallback;
    }
}

inline void append_bounded(std::string& target, const char* data, std::size_t size, std::size_t max_bytes) {
    if (target.size() >= max_bytes) {
        return;
    }
    const auto remaining = max_bytes - target.size();
    target.append(data, (std::min)(size, remaining));
    if (target.size() >= max_bytes) {
        target += "\n[truncated: max_output_bytes " + std::to_string(max_bytes) + "]";
    }
}

inline std::string format_result(int exit_code,
                                 bool timeout,
                                 const std::string& stdout_text,
                                 const std::string& stderr_text,
                                 const std::string& full_output_path = {}) {
    std::ostringstream out;
    out << "exit_code: " << exit_code << '\n';
    out << "timeout: " << (timeout ? "true" : "false") << '\n';
    if (!full_output_path.empty()) {
        out << "full_output: " << full_output_path << '\n';
    }
    out << "stdout:\n" << stdout_text << '\n';
    out << "stderr:\n" << stderr_text << '\n';
    return out.str();
}

inline std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

inline std::string preview_output(const std::string& full_output, std::size_t max_output_bytes) {
    std::string preview;
    append_bounded(preview, full_output.data(), full_output.size(), max_output_bytes);
    return preview;
}

inline std::filesystem::path shell_output_path(const std::filesystem::path& workspace_root) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = workspace_root / ".agent_tui" / "shell-output";
    std::filesystem::create_directories(dir);
    return dir / ("run-" + std::to_string(stamp) + ".txt");
}

#ifdef _WIN32
inline std::wstring widen_ascii(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}
#else
inline bool set_nonblocking(int fd, std::string& error) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        error = std::strerror(errno);
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        error = std::strerror(errno);
        return false;
    }
    return true;
}

inline void drain_fd(int& fd, std::string& preview, std::string& full, std::size_t max_output_bytes) {
    if (fd < 0) {
        return;
    }

    char buffer[4096];
    while (true) {
        const ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            full.append(buffer, static_cast<std::size_t>(n));
            append_bounded(preview, buffer, static_cast<std::size_t>(n), max_output_bytes);
            continue;
        }
        if (n == 0) {
            close(fd);
            fd = -1;
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        close(fd);
        fd = -1;
        return;
    }
}
#endif

}  // namespace shell_tool_detail

class ShellTool final : public Tool {
public:
    explicit ShellTool(const Workspace& workspace) : workspace_(workspace) {}

    std::string name() const override { return "run_shell"; }
    std::string description() const override { return "Run a shell command inside the workspace."; }
    std::string parameters_schema_json() const override {
        return R"({"type":"object","properties":{"command":{"type":"string","description":"Shell command to run"},"cwd":{"type":"string","description":"Workspace-relative working directory, defaults to ."},"timeout_seconds":{"type":"string","description":"Timeout in seconds"},"max_output_bytes":{"type":"string","description":"Maximum output bytes to return"}},"required":["command"]})";
    }
    PermissionMode permission_mode() const override { return PermissionMode::Confirm; }

    ToolResult run(const JsonLike& arguments) override {
        const auto command = shell_tool_detail::get_arg(arguments, "command");
        if (command.empty()) {
            return ToolResult::failure("missing required argument: command");
        }

        const auto timeout_seconds = (std::max)(1, shell_tool_detail::get_int_arg(arguments, "timeout_seconds", 30));
        const auto max_output_bytes = shell_tool_detail::get_size_arg(arguments, "max_output_bytes", 64 * 1024);

        std::filesystem::path cwd;
        try {
            cwd = workspace_.resolve(shell_tool_detail::get_arg(arguments, "cwd", "."));
        } catch (const std::exception& error) {
            return ToolResult::failure(error.what());
        }

        if (!std::filesystem::is_directory(cwd)) {
            return ToolResult::failure("cwd is not a directory: " + workspace_.display_path(cwd));
        }

#ifdef _WIN32
        return run_windows(command, cwd, timeout_seconds, max_output_bytes);
#else
        return run_posix(command, cwd, timeout_seconds, max_output_bytes);
#endif
    }

private:
#ifdef _WIN32
    ToolResult run_windows(const std::string& command,
                           const std::filesystem::path& cwd,
                           int timeout_seconds,
                           std::size_t max_output_bytes) {
        (void)timeout_seconds;
        const auto output_path = shell_tool_detail::shell_output_path(workspace_.root());

        SECURITY_ATTRIBUTES security_attributes{};
        security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
        security_attributes.bInheritHandle = TRUE;

        HANDLE output_file = CreateFileW(output_path.wstring().c_str(),
                                         GENERIC_WRITE,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         &security_attributes,
                                         CREATE_ALWAYS,
                                         FILE_ATTRIBUTE_NORMAL,
                                         nullptr);
        if (output_file == INVALID_HANDLE_VALUE) {
            return ToolResult::failure("failed to create shell output file");
        }

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = output_file;
        startup.hStdError = output_file;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

        PROCESS_INFORMATION process{};
        const auto command_line_text = std::wstring{L"cmd.exe /D /S /C \"cd /d \"\""} + cwd.wstring() +
                                       L"\"\" && " + shell_tool_detail::widen_ascii(command) + L"\"";
        std::vector<wchar_t> command_line(command_line_text.begin(), command_line_text.end());
        command_line.push_back(L'\0');

        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (job == nullptr) {
            return ToolResult::failure("failed to create shell job object");
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_limits{};
        job_limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &job_limits, sizeof(job_limits));

        const BOOL started = CreateProcessW(nullptr,
                                            command_line.data(),
                                            nullptr,
                                            nullptr,
                                            TRUE,
                                            CREATE_NO_WINDOW | CREATE_SUSPENDED,
                                            nullptr,
                                            nullptr,
                                            &startup,
                                            &process);
        CloseHandle(output_file);

        if (!started) {
            CloseHandle(job);
            return ToolResult::failure("failed to start command with CreateProcessW");
        }

        AssignProcessToJobObject(job, process.hProcess);
        ResumeThread(process.hThread);

        const DWORD wait_result = WaitForSingleObject(process.hProcess, static_cast<DWORD>(timeout_seconds) * 1000);
        bool timeout = false;
        if (wait_result == WAIT_TIMEOUT) {
            timeout = true;
            TerminateJobObject(job, 1);
            WaitForSingleObject(process.hProcess, 5000);
        }

        DWORD raw_exit_code = 1;
        GetExitCodeProcess(process.hProcess, &raw_exit_code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(job);

        const auto full_output = shell_tool_detail::read_text_file(output_path);
        const auto preview = shell_tool_detail::preview_output(full_output, max_output_bytes);
        const auto full_output_path = full_output.size() > max_output_bytes ? output_path.generic_string() : std::string{};
        return ToolResult::success(shell_tool_detail::format_result(static_cast<int>(raw_exit_code),
                                                                    timeout,
                                                                    preview,
                                                                    {},
                                                                    full_output_path));
    }
#else
    ToolResult run_posix(const std::string& command,
                         const std::filesystem::path& cwd,
                         int timeout_seconds,
                         std::size_t max_output_bytes) {
        int stdout_pipe[2];
        int stderr_pipe[2];
        if (pipe(stdout_pipe) == -1) {
            return ToolResult::failure(std::string{"failed to create stdout pipe: "} + std::strerror(errno));
        }
        if (pipe(stderr_pipe) == -1) {
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            return ToolResult::failure(std::string{"failed to create stderr pipe: "} + std::strerror(errno));
        }

        const pid_t pid = fork();
        if (pid == -1) {
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
            return ToolResult::failure(std::string{"failed to fork: "} + std::strerror(errno));
        }

        if (pid == 0) {
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);
            close(stdout_pipe[1]);
            close(stderr_pipe[1]);
            if (chdir(cwd.c_str()) != 0) {
                _exit(126);
            }
            execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }

        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        int stdout_fd = stdout_pipe[0];
        int stderr_fd = stderr_pipe[0];

        std::string error;
        if (!shell_tool_detail::set_nonblocking(stdout_fd, error) || !shell_tool_detail::set_nonblocking(stderr_fd, error)) {
            kill(pid, SIGKILL);
            close(stdout_fd);
            close(stderr_fd);
            return ToolResult::failure("failed to set nonblocking pipes: " + error);
        }

        std::string stdout_text;
        std::string stderr_text;
        std::string full_stdout;
        std::string full_stderr;
        bool timeout = false;
        int status = 0;
        bool child_exited = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);

        while (stdout_fd >= 0 || stderr_fd >= 0 || !child_exited) {
            if (stdout_fd >= 0) {
                shell_tool_detail::drain_fd(stdout_fd, stdout_text, full_stdout, max_output_bytes);
            }
            if (stderr_fd >= 0) {
                shell_tool_detail::drain_fd(stderr_fd, stderr_text, full_stderr, max_output_bytes);
            }

            if (!child_exited) {
                const pid_t wait_result = waitpid(pid, &status, WNOHANG);
                if (wait_result == pid) {
                    child_exited = true;
                } else if (wait_result == -1) {
                    return ToolResult::failure(std::string{"waitpid failed: "} + std::strerror(errno));
                }
            }

            if (!child_exited && std::chrono::steady_clock::now() >= deadline) {
                timeout = true;
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                child_exited = true;
            }

            if (child_exited && stdout_fd < 0 && stderr_fd < 0) {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        int exit_code = -1;
        if (!timeout) {
            if (WIFEXITED(status)) {
                exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                exit_code = 128 + WTERMSIG(status);
            }
        }

        std::string full_output_path;
        if (full_stdout.size() + full_stderr.size() > max_output_bytes) {
            const auto output_path = shell_tool_detail::shell_output_path(workspace_.root());
            std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
            output << full_stdout;
            output << full_stderr;
            full_output_path = output_path.generic_string();
        }

        return ToolResult::success(shell_tool_detail::format_result(exit_code, timeout, stdout_text, stderr_text, full_output_path));
    }
#endif

    const Workspace& workspace_;
};

}  // namespace agent_tui
