#include "ProcessIO.h"
#include <chrono>
#include <signal.h>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <sys/times.h>
#include <fcntl.h>
#include <signal.h>
#endif
using std::min;
std::string expand_percent_vars(
    std::string_view input,
    const std::unordered_map<std::string, std::string>& vars)
{
    std::string out;
    out.reserve(input.size());

    for (size_t i = 0; i < input.size(); )
    {
        if (input[i] == '%')
        {
            size_t end = input.find('%', i + 1);
            if (end != std::string_view::npos)
            {
                std::string key(input.substr(i + 1, end - i - 1));
                auto it = vars.find(key);
                if (it != vars.end())
                    out += it->second;
                else
                    out.append(input.substr(i, end - i + 1)); // leave unchanged

                i = end + 1;
                continue;
            }
        }

        out += input[i++];
    }

    return out;
}

ProcessResult run_command(
    const std::vector<std::string>& command,
    const fs::path& cwd,
    const std::string& stdin_data,
    const float time_limit_sec,
    const int maxMemoryMB)
{
    const std::size_t maxOutputBytes = (std::size_t)32 * 1024 * 1024;

    auto start_wall = std::chrono::high_resolution_clock::now();

#ifdef _WIN32

    HANDLE inRd, inWr, outRd, outWr, errRd, errWr;
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };

    if (!CreatePipe(&inRd, &inWr, &sa, 0) ||
        !CreatePipe(&outRd, &outWr, &sa, 0) ||
        !CreatePipe(&errRd, &errWr, &sa, 0))
        throw CPError<CPErrors::IE>("pipe creation failed");

    SetHandleInformation(inRd, 0, HANDLE_FLAG_INHERIT);
    SetHandleInformation(outWr, 0, HANDLE_FLAG_INHERIT);
    SetHandleInformation(errWr, 0, HANDLE_FLAG_INHERIT);

    std::wstring cmdline;
    for (size_t i = 0; i < command.size(); ++i) {
        if (i) cmdline += L" ";
        int sz = MultiByteToWideChar(CP_UTF8, 0, command[i].c_str(), -1, nullptr, 0);
        std::wstring part(sz - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, command[i].c_str(), -1, part.data(), sz);
        cmdline += part;
    }

    PROCESS_INFORMATION pi{};
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = inRd;
    si.hStdOutput = outWr;
    si.hStdError  = errWr;

    std::wstring wcwd = cwd.wstring();

    if (!CreateProcessW(
            nullptr,
            cmdline.data(),
            nullptr,
            nullptr,
            TRUE,
            0,
            nullptr,
            wcwd.c_str(),
            &si,
            &pi))
        throw CPError<CPErrors::IE>("failed to spawn process");

    CloseHandle(inRd);
    CloseHandle(outWr);
    CloseHandle(errWr);

    if (!stdin_data.empty()) {
        DWORD written;
        WriteFile(inWr, stdin_data.data(),
                  (DWORD)stdin_data.size(), &written, nullptr);
    }
    CloseHandle(inWr);

    std::string out_buf, err_buf;
    char buffer[4096];

    while (true) {

        // Wait up to 10ms for process exit
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 10);

        // Drain stdout
        DWORD avail = 0;
        while (PeekNamedPipe(outRd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD nread;
            if (!ReadFile(outRd, buffer,
                          min<DWORD>((DWORD)sizeof(buffer), avail),
                          &nread, nullptr))
                break;

            out_buf.append(buffer, nread);
            if (out_buf.size() > maxOutputBytes) {
                TerminateProcess(pi.hProcess, 1);
                Sleep(1000);
                throw CPError<CPErrors::OLE>();
            }
        }

        // Drain stderr
        while (PeekNamedPipe(errRd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD nread;
            if (!ReadFile(errRd, buffer,
                          min<DWORD>((DWORD)sizeof(buffer), avail),
                          &nread, nullptr))
                break;

            err_buf.append(buffer, nread);
            if (err_buf.size() > maxOutputBytes) {
                TerminateProcess(pi.hProcess, 1);
                Sleep(1000);
                throw CPError<CPErrors::OLE>();
            }
        }

        // Check CPU time
        FILETIME ftCreate, ftExit, ftKernel, ftUser;
        GetProcessTimes(pi.hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser);

        ULARGE_INTEGER k{}, u{};
        k.LowPart  = ftKernel.dwLowDateTime;
        k.HighPart = ftKernel.dwHighDateTime;
        u.LowPart  = ftUser.dwLowDateTime;
        u.HighPart = ftUser.dwHighDateTime;

        float cpu_secs =
            (float)((k.QuadPart + u.QuadPart) * 1e-7);

        if (cpu_secs > time_limit_sec) {
            TerminateProcess(pi.hProcess, 1);
            Sleep(1000);
            throw CPError<CPErrors::TLE>();
        }

        // Check wall time
        auto now_wall = std::chrono::steady_clock::now();
        float wall_secs =
            std::chrono::duration<float>(now_wall - start_wall).count();

        if (wall_secs > time_limit_sec) {
            TerminateProcess(pi.hProcess, 1);
            Sleep(1000);
            throw CPError<CPErrors::TLE>();
        }

        if (waitResult == WAIT_OBJECT_0)
            break;
    }

    DWORD exit_code;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    FILETIME ftCreate, ftExit, ftKernel, ftUser;
    GetProcessTimes(pi.hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser);

    ULARGE_INTEGER k{}, u{};
    k.LowPart  = ftKernel.dwLowDateTime;
    k.HighPart = ftKernel.dwHighDateTime;
    u.LowPart  = ftUser.dwLowDateTime;
    u.HighPart = ftUser.dwHighDateTime;

    float cpu_secs =
        (float)((k.QuadPart + u.QuadPart) * 1e-7);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(outRd);
    CloseHandle(errRd);

    if (cpu_secs > time_limit_sec){
        Sleep(1000);
        throw CPError<CPErrors::TLE>();
    }
    return { out_buf, err_buf, (uint32_t)exit_code, cpu_secs };
#else  // POSIX

    int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0)
        throw CPError<CPErrors::IE>("pipe creation failed");

    pid_t pid = fork();
    if (pid < 0)
        throw CPError<CPErrors::IE>("fork failed");

    if (pid == 0) {
        // Child process
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        std::vector<char*> argv;
        for (const auto &s : command)
            argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);

        chdir(cwd.c_str());
        execvp(argv[0], argv.data());
        _exit(127);
    }

    // Parent process
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    // Write stdin data if provided
    if (!stdin_data.empty())
        write(stdin_pipe[1], stdin_data.data(), stdin_data.size());
    close(stdin_pipe[1]);

    std::string out_buf, err_buf;
    char buf[4096];
    bool finished = false;

    while (!finished) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(stdout_pipe[0], &fds);
        FD_SET(stderr_pipe[0], &fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 10000; // 10ms timeout for select

        select(std::max(stdout_pipe[0], stderr_pipe[0]) + 1, &fds, nullptr, nullptr, &tv);

        // Read from stdout
        if (FD_ISSET(stdout_pipe[0], &fds)) {
            ssize_t r = read(stdout_pipe[0], buf, sizeof(buf));
            if (r > 0) {
                out_buf.append(buf, (size_t)r);
                if (out_buf.size() > maxOutputBytes) {
                    kill(pid, SIGKILL);
                    waitpid(pid, nullptr, 0); // Reap zombie
                    throw CPError<CPErrors::OLE>();
                }
            }
        }

        // Read from stderr
        if (FD_ISSET(stderr_pipe[0], &fds)) {
            ssize_t r = read(stderr_pipe[0], buf, sizeof(buf));
            if (r > 0) {
                err_buf.append(buf, (size_t)r);
                if (err_buf.size() > maxOutputBytes) {
                    kill(pid, SIGKILL);
                    waitpid(pid, nullptr, 0); // Reap zombie
                    throw CPError<CPErrors::OLE>();
                }
            }
        }

        // Check if child has exited (non-blocking)
        int status;
        struct rusage usage;
        pid_t rv = wait4(pid, &status, WNOHANG, &usage);
        
        if (rv == pid) {
            finished = true;

            auto end_wall = std::chrono::high_resolution_clock::now();
            float wall_secs = std::chrono::duration<float>(end_wall - start_wall).count();

            // CPU time from the child-specific rusage (not accumulated across calls)
            float user_cpu_time = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6;
            float system_cpu_time = usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
            float total_cpu_time = user_cpu_time + system_cpu_time;
            float cpu_secs = total_cpu_time;

            uint32_t ec = 0;
            if (WIFEXITED(status))
                ec = (uint32_t)WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                ec = 128 + (uint32_t)WTERMSIG(status); // Common convention for signal exit

            // Check CPU time limit after process exits
            if (cpu_secs > time_limit_sec) {
                throw CPError<CPErrors::TLE>();
            }

            // Debug output (optional)
            std::cerr << "Wall: " << wall_secs << "s, CPU: " << cpu_secs << "s\n";

            // Close remaining pipes
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);

            return { out_buf, err_buf, ec, cpu_secs };
        }

        // Check wall time limit
        auto now_wall = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(now_wall - start_wall).count();
        
        if (elapsed > time_limit_sec) {
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0); // Reap zombie
            throw CPError<CPErrors::TLE>();
        }
    }

    // Should never reach here
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    throw CPError<CPErrors::IE>("unexpected termination");
#endif
}
