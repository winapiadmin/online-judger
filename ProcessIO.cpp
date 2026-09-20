#include "ProcessIO.h"
#include <chrono>
#include <iostream>
#include <mutex>
#include <signal.h>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
using std::min;
std::string
expand_percent_vars(std::string_view input,
                    const std::unordered_map<std::string, std::string> &vars) {
  std::string out;
  out.reserve(input.size());

  for (size_t i = 0; i < input.size();) {
    if (input[i] == '%') {
      size_t end = input.find('%', i + 1);
      if (end != std::string_view::npos) {
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

#ifdef _WIN32
namespace {
struct HandleGuard {
  HANDLE h = nullptr;
  explicit HandleGuard(HANDLE handle = nullptr) : h(handle) {}
  ~HandleGuard() {
    if (h)
      CloseHandle(h);
  }
  HandleGuard(const HandleGuard &) = delete;
  HandleGuard &operator=(const HandleGuard &) = delete;
  void release() { h = nullptr; }
};

// Quotes an argument per the MSVCRT command-line rules so that arguments
// containing spaces survive CreateProcessW round-trip.
std::wstring quote_arg(const std::wstring &a) {
  if (a.find_first_of(L" \t\"") == std::wstring::npos)
    return a;

  std::wstring r = L"\"";
  size_t backslashes = 0;
  for (wchar_t c : a) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    if (c == L'"') {
      r.append(backslashes * 2 + 1, L'\\');
      r += L'"';
    } else {
      r.append(backslashes, L'\\');
      r += c;
    }
    backslashes = 0;
  }
  r.append(backslashes * 2, L'\\');
  r += L'"';
  return r;
}
} // namespace
#endif

ProcessResult run_command(const std::vector<std::string> &command,
                          const fs::path &cwd, const std::string &stdin_data,
                          const float time_limit_sec, const int maxMemoryMB) {
  const std::size_t maxOutputBytes = (std::size_t)32 * 1024 * 1024;

  auto start_wall = std::chrono::high_resolution_clock::now();

#ifdef _WIN32

  HandleGuard inRd, inWr, outRd, outWr, errRd, errWr;
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};

  if (!CreatePipe(&inRd.h, &inWr.h, &sa, 0) ||
      !CreatePipe(&outRd.h, &outWr.h, &sa, 0) ||
      !CreatePipe(&errRd.h, &errWr.h, &sa, 0))
    throw CPError<CPErrors::IE>("pipe creation failed");

  // Mark the child's three std ends inheritable. The mask MUST name the
  // INHERIT flag; a zero mask is a silent no-op and leaves the handles
  // uninheritable, producing dead stdio in the child.
  SetHandleInformation(inRd.h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
  SetHandleInformation(outWr.h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
  SetHandleInformation(errWr.h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

  std::wstring cmdline;
  for (size_t i = 0; i < command.size(); ++i) {
    if (i)
      cmdline += L" ";
    int sz =
        MultiByteToWideChar(CP_UTF8, 0, command[i].c_str(), -1, nullptr, 0);
    if (sz <= 0)
      throw CPError<CPErrors::IE>("UTF-8 to UTF-16 conversion failed");
    std::wstring part(sz - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, command[i].c_str(), -1, part.data(), sz);
    cmdline += quote_arg(part);
  }

  PROCESS_INFORMATION pi{};
  // Restrict handle inheritance to exactly the three std ends; otherwise the
  // child also inherits the pipe WRITE ends and stdin never reaches EOF
  // (child blocks forever reading its own open write end).
  STARTUPINFOEXW si{};
  si.StartupInfo.cb = sizeof(si);
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  si.StartupInfo.hStdInput = inRd.h;
  si.StartupInfo.hStdOutput = outWr.h;
  si.StartupInfo.hStdError = errWr.h;

  SIZE_T attrSize = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
  std::vector<char> attrBuf(attrSize);
  si.lpAttributeList =
      reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
  if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize))
    throw CPError<CPErrors::IE>("InitializeProcThreadAttributeList failed");

  HANDLE inheritHandles[3] = {inRd.h, outWr.h, errWr.h};
  BOOL attrOk = UpdateProcThreadAttribute(
      si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inheritHandles,
      sizeof(inheritHandles), nullptr, nullptr);

  std::wstring wcwd = cwd.wstring();

  BOOL created = FALSE;
  // Start suspended so the job object (memory cap) is attached before the
  // child's first instruction.
  if (attrOk)
    created = CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
                             EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED,
                             nullptr, wcwd.c_str(), &si.StartupInfo, &pi);
  DeleteProcThreadAttributeList(si.lpAttributeList);

  if (!created)
    throw CPError<CPErrors::IE>("failed to spawn process");

  // Enforce the memory limit via a job object when a positive cap is given.
  HandleGuard jobGuard;
  if (maxMemoryMB > 0) {
    jobGuard.h = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
    jeli.ProcessMemoryLimit = (SIZE_T)maxMemoryMB << 20;
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    if (!jobGuard.h ||
        !SetInformationJobObject(jobGuard.h, JobObjectExtendedLimitInformation,
                                 &jeli, sizeof(jeli)) ||
        !AssignProcessToJobObject(jobGuard.h, pi.hProcess))
      throw CPError<CPErrors::IE>("failed to apply memory limit");
  }

  if (ResumeThread(pi.hThread) == (DWORD)-1)
    throw CPError<CPErrors::IE>("failed to resume child process");

  HandleGuard procGuard(pi.hProcess), threadGuard(pi.hThread);

  // The child owns its ends now.
  inRd.release();
  outWr.release();
  errWr.release();

  // Feed stdin from a helper thread; a blocking WriteFile here would deadlock
  // against a child that fills its stdout pipe before reading all input.
  std::thread stdinWriter([&inWr, &stdin_data] {
    DWORD total = 0;
    while (total < stdin_data.size()) {
      DWORD written = 0;
      if (!WriteFile(inWr.h, stdin_data.data() + total,
                     (DWORD)(stdin_data.size() - total), &written, nullptr))
        break;
      total += written;
    }
    if (inWr.h) {
      CloseHandle(inWr.h);
      inWr.h = nullptr;
    }
  });
  auto joinStdin = [&stdinWriter] {
    if (stdinWriter.joinable())
      stdinWriter.join();
  };
  struct JoinOnExit {
    std::thread *t;
    ~JoinOnExit() {
      if (t && t->joinable())
        t->join();
    }
  } joiner{&stdinWriter};

  std::string out_buf, err_buf;
  char buffer[4096];

  while (true) {

    // Wait up to 10ms for process exit
    DWORD waitResult = WaitForSingleObject(pi.hProcess, 10);

    // Drain stdout
    DWORD avail = 0;
    while (PeekNamedPipe(outRd.h, nullptr, 0, nullptr, &avail, nullptr) &&
           avail > 0) {
      DWORD nread;
      if (!ReadFile(outRd.h, buffer, min<DWORD>((DWORD)sizeof(buffer), avail),
                    &nread, nullptr))
        break;

      out_buf.append(buffer, nread);
      if (out_buf.size() > maxOutputBytes) {
        TerminateProcess(pi.hProcess, 1);
        Sleep(1000);
        joinStdin();
        throw CPError<CPErrors::OLE>();
      }
    }

    // Drain stderr
    while (PeekNamedPipe(errRd.h, nullptr, 0, nullptr, &avail, nullptr) &&
           avail > 0) {
      DWORD nread;
      if (!ReadFile(errRd.h, buffer, min<DWORD>((DWORD)sizeof(buffer), avail),
                    &nread, nullptr))
        break;

      err_buf.append(buffer, nread);
      if (err_buf.size() > maxOutputBytes) {
        TerminateProcess(pi.hProcess, 1);
        Sleep(1000);
        joinStdin();
        throw CPError<CPErrors::OLE>();
      }
    }

    // Check CPU time
    FILETIME ftCreate, ftExit, ftKernel, ftUser;
    GetProcessTimes(pi.hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser);

    ULARGE_INTEGER k{}, u{};
    k.LowPart = ftKernel.dwLowDateTime;
    k.HighPart = ftKernel.dwHighDateTime;
    u.LowPart = ftUser.dwLowDateTime;
    u.HighPart = ftUser.dwHighDateTime;

    float cpu_secs = (float)((k.QuadPart + u.QuadPart) * 1e-7);

    if (cpu_secs > time_limit_sec) {
      TerminateProcess(pi.hProcess, 1);
      Sleep(1000);
      joinStdin();
      throw CPError<CPErrors::TLE>();
    }

    // Check wall time
    auto now_wall = std::chrono::high_resolution_clock::now();
    float wall_secs =
        std::chrono::duration<float>(now_wall - start_wall).count();

    if (wall_secs > time_limit_sec) {
      TerminateProcess(pi.hProcess, 1);
      Sleep(1000);
      joinStdin();
      throw CPError<CPErrors::TLE>();
    }

    if (waitResult == WAIT_OBJECT_0)
      break;
  }

  joinStdin();

  DWORD exit_code;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  FILETIME ftCreate, ftExit, ftKernel, ftUser;
  GetProcessTimes(pi.hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser);

  ULARGE_INTEGER k{}, u{};
  k.LowPart = ftKernel.dwLowDateTime;
  k.HighPart = ftKernel.dwHighDateTime;
  u.LowPart = ftUser.dwLowDateTime;
  u.HighPart = ftUser.dwHighDateTime;

  float cpu_secs = (float)((k.QuadPart + u.QuadPart) * 1e-7);

  if (cpu_secs > time_limit_sec)
    throw CPError<CPErrors::TLE>();
  return {out_buf, err_buf, (uint32_t)exit_code, cpu_secs};
#else // POSIX

  {
    static std::once_flag sigpipe_once;
    std::call_once(sigpipe_once,
                   [] { signal(SIGPIPE, SIG_IGN); }); // EPIPE instead of death
  }

  int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];
  if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0)
    throw CPError<CPErrors::IE>("pipe creation failed");

  pid_t pid = fork();
  if (pid < 0)
    throw CPError<CPErrors::IE>("fork failed");

  if (pid == 0) {
    // Child process: own process group so the parent can kill the whole tree
    setpgid(0, 0);

    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    // Address-space cap; malloc/brk fail once exceeded (child gets NULL /
    // crashes instead of exhausting host memory).
    if (maxMemoryMB > 0) {
      struct rlimit rl;
      rl.rlim_cur = rl.rlim_max = (rlim_t)maxMemoryMB * 1024 * 1024;
      setrlimit(RLIMIT_AS, &rl);
    }

    std::vector<char *> argv;
    for (const auto &s : command)
      argv.push_back(const_cast<char *>(s.c_str()));
    argv.push_back(nullptr);

    if (chdir(cwd.c_str()) != 0)
      _exit(126);
    execvp(argv[0], argv.data());
    _exit(127);
  }

  /* Mirror the child's setpgid so killpg() cannot race with it. */
  setpgid(pid, pid);

  // Parent process
  close(stdin_pipe[0]);
  close(stdout_pipe[1]);
  close(stderr_pipe[1]);

  // Feed stdin from a helper thread; a blocking write here would deadlock
  // against a child that fills its stdout pipe before reading all input.
  std::thread stdinWriter([&stdin_pipe, &stdin_data] {
    size_t total = 0;
    while (total < stdin_data.size()) {
      ssize_t w =
          write(stdin_pipe[1], stdin_data.data() + total,
                min(stdin_data.size() - total, (size_t)65536));
      if (w <= 0)
        break; // child died or closed stdin
      total += w;
    }
    close(stdin_pipe[1]);
  });
  struct JoinStdin {
    std::thread *t;
    ~JoinStdin() {
      if (t->joinable())
        t->join();
    }
  } joinStdin{&stdinWriter};

  std::string out_buf, err_buf;
  char buf[4096];
  bool finished = false;

  auto drain_fd = [&](int fd, std::string &into) -> bool {
    // returns false when the limit is exceeded
    ssize_t r = read(fd, buf, sizeof(buf));
    if (r > 0) {
      into.append(buf, (size_t)r);
      if (into.size() > maxOutputBytes)
        return false;
    }
    return true;
  };

  while (!finished) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(stdout_pipe[0], &fds);
    FD_SET(stderr_pipe[0], &fds);
    int maxfd = std::max(stdout_pipe[0], stderr_pipe[0]);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10000; // 10ms timeout for select

    select(maxfd + 1, &fds, nullptr, nullptr, &tv);

    // Read from stdout
    if (FD_ISSET(stdout_pipe[0], &fds)) {
      if (!drain_fd(stdout_pipe[0], out_buf)) {
        killpg(pid, SIGKILL); /* whole group: orphans die too */
        waitpid(pid, nullptr, 0); // Reap zombie
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        throw CPError<CPErrors::OLE>();
      }
    }

    // Read from stderr
    if (FD_ISSET(stderr_pipe[0], &fds)) {
      if (!drain_fd(stderr_pipe[0], err_buf)) {
        killpg(pid, SIGKILL); /* whole group: orphans die too */
        waitpid(pid, nullptr, 0); // Reap zombie
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        throw CPError<CPErrors::OLE>();
      }
    }

    // Check if child has exited (non-blocking)
    int status;
    struct rusage usage;
    pid_t rv = wait4(pid, &status, WNOHANG, &usage);

    if (rv == pid) {
      finished = true;

      auto end_wall = std::chrono::high_resolution_clock::now();
      float wall_secs =
          std::chrono::duration<float>(end_wall - start_wall).count();

      // CPU time from the child-specific rusage (not accumulated across calls)
      float user_cpu_time =
          usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6;
      float system_cpu_time =
          usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
      float cpu_secs = user_cpu_time + system_cpu_time;

      uint32_t ec = 0;
      if (WIFEXITED(status))
        ec = (uint32_t)WEXITSTATUS(status);
      else if (WIFSIGNALED(status))
        ec = 128 +
             (uint32_t)WTERMSIG(status); // Common convention for signal exit

      // Drain whatever is still buffered in the pipes until EOF so output is
      // not truncated. Bounded by a grace period: an orphaned grandchild may
      // hold the write ends open forever, and we must not hang on it.
      constexpr float kDrainGraceSec = 1.0f;
      bool overflow = false;
      bool open_out = true, open_err = true;
      auto drain_deadline = std::chrono::high_resolution_clock::now() +
                            std::chrono::milliseconds(
                                (long long)(kDrainGraceSec * 1000));
      while ((open_out || open_err) && !overflow) {
        fd_set drain_fds;
        FD_ZERO(&drain_fds);
        if (open_out)
          FD_SET(stdout_pipe[0], &drain_fds);
        if (open_err)
          FD_SET(stderr_pipe[0], &drain_fds);

        auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
                             drain_deadline - std::chrono::high_resolution_clock::now())
                             .count();
        if (remaining <= 0)
          break; // orphaned writer; keep what we have

        struct timeval dtv;
        dtv.tv_sec = (long)(remaining / 1000000);
        dtv.tv_usec = (long)(remaining % 1000000);
        select(std::max(stdout_pipe[0], stderr_pipe[0]) + 1, &drain_fds,
               nullptr, nullptr, &dtv);

        if (open_out && FD_ISSET(stdout_pipe[0], &drain_fds)) {
          ssize_t r = read(stdout_pipe[0], buf, sizeof(buf));
          if (r > 0) {
            out_buf.append(buf, (size_t)r);
            if (out_buf.size() > maxOutputBytes)
              overflow = true;
          } else
            open_out = false;
        }
        if (!overflow && open_err && FD_ISSET(stderr_pipe[0], &drain_fds)) {
          ssize_t r = read(stderr_pipe[0], buf, sizeof(buf));
          if (r > 0) {
            err_buf.append(buf, (size_t)r);
            if (err_buf.size() > maxOutputBytes)
              overflow = true;
          } else
            open_err = false;
        }
      }

      close(stdout_pipe[0]);
      close(stderr_pipe[0]);

      if (overflow)
        throw CPError<CPErrors::OLE>();

      // Check CPU time limit after process exits
      if (cpu_secs > time_limit_sec)
        throw CPError<CPErrors::TLE>();

      return {out_buf, err_buf, ec, cpu_secs};
    }

    // Check wall time limit
    auto now_wall = std::chrono::high_resolution_clock::now();
    float elapsed = std::chrono::duration<float>(now_wall - start_wall).count();

    if (elapsed > time_limit_sec) {
      killpg(pid, SIGKILL); /* whole group: orphans die too */
      waitpid(pid, nullptr, 0); // Reap zombie
      close(stdout_pipe[0]);
      close(stderr_pipe[0]);
      throw CPError<CPErrors::TLE>();
    }
  }

  // Should never reach here
  close(stdout_pipe[0]);
  close(stderr_pipe[0]);
  throw CPError<CPErrors::IE>("unexpected termination");
#endif
}
