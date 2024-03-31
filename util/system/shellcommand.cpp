#include "shellcommand.h"

#include <util/generic/yexception.h>
#include <util/generic/algorithm.h>
#include <util/generic/buffer.h>
#include <util/memory/tempbuf.h>
#include "file.h"
#include "user.h"
#include "nice.h"
#include "sigset.h"
#include <util/folder/dirut.h>
#include <util/network/socket.h>
#include <util/stream/str.h>
#include <util/system/info.h>
#include <util/stream/pipe.h>

#include <errno.h>

#if defined(_unix_)
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

using TPid = pid_t;
using TWaitResult = pid_t;
using TExitStatus = int;
#define WAIT_PROCEED 0
#elif defined(_win_)
#include <string>

#include "winint.h"

using TPid = HANDLE;
using TWaitResult = DWORD;
using TExitStatus = DWORD;
#define WAIT_PROCEED WAIT_TIMEOUT

#pragma warning(disable : 4296) // 'wait_result >= WAIT_OBJECT_0' : expression is always tru
#else
#error("unknown os, shell command is not implemented")
#endif

#define DBG(stmt) \
    {}
// #define DBG(stmt) stmt

namespace {
#if defined(_unix_)
    void ImpersonateUser(const TString& userName) {
        if (GetUsername() == userName) {
            return;
        }
        const passwd* newUser = getpwnam(userName.c_str());
        if (!newUser) {
            ythrow TSystemError(errno) << "getpwnam failed";
        }
        if (setuid(newUser->pw_uid)) {
            ythrow TSystemError(errno) << "setuid failed";
        }
    }
#elif defined(_win_)
    constexpr static size_t MAX_COMMAND_LINE = 32 * 1024;

    std::wstring GetWString(const char* astring) {
        if (!astring)
            return std::wstring();

        std::string str(astring);
        return std::wstring(str.begin(), str.end());
    }

    std::string GetAString(const wchar_t* wstring) {
        if (!wstring)
            return std::string();

        std::wstring str(wstring);
        return std::string(str.begin(), str.end());
    }
#endif
}

// temporary measure to avoid rewriting all poll calls on win TPipeHandle
#if defined(_win_)
using REALPIPEHANDLE = HANDLE;
#define INVALID_REALPIPEHANDLE INVALID_HANDLE_VALUE

class TRealPipeHandle
   : public TNonCopyable {
public:
    inline TRealPipeHandle() noexcept
        : Fd_(INVALID_REALPIPEHANDLE)
    {
    }

    inline TRealPipeHandle(REALPIPEHANDLE fd) noexcept
        : Fd_(fd)
    {
    }

    inline ~TRealPipeHandle() {
        Close();
    }

    bool Close() noexcept {
        bool ok = true;
        if (Fd_ != INVALID_REALPIPEHANDLE)
            ok = CloseHandle(Fd_);
        Fd_ = INVALID_REALPIPEHANDLE;
        return ok;
    }

    inline REALPIPEHANDLE Release() noexcept {
        REALPIPEHANDLE ret = Fd_;
        Fd_ = INVALID_REALPIPEHANDLE;
        return ret;
    }

    inline void Swap(TRealPipeHandle& r) noexcept {
        DoSwap(Fd_, r.Fd_);
    }

    inline operator REALPIPEHANDLE() const noexcept {
        return Fd_;
    }

    inline bool IsOpen() const noexcept {
        return Fd_ != INVALID_REALPIPEHANDLE;
    }

    ssize_t Read(void* buffer, size_t byteCount) const noexcept {
        DWORD doneBytes;
        if (!ReadFile(Fd_, buffer, byteCount, &doneBytes, nullptr))
            return -1;
        return doneBytes;
    }
    ssize_t Write(const void* buffer, size_t byteCount) const noexcept {
        DWORD doneBytes;
        if (!WriteFile(Fd_, buffer, byteCount, &doneBytes, nullptr))
            return -1;
        return doneBytes;
    }

    static void Pipe(TRealPipeHandle& reader, TRealPipeHandle& writer) {
        REALPIPEHANDLE fds[2];
        if (!CreatePipe(&fds[0], &fds[1], nullptr, 0))
            ythrow TFileError() << "failed to create a pipe";
        TRealPipeHandle(fds[0]).Swap(reader);
        TRealPipeHandle(fds[1]).Swap(writer);
    }

private:
    REALPIPEHANDLE Fd_;
};

#else
using TRealPipeHandle = TPipeHandle;
using REALPIPEHANDLE = PIPEHANDLE;
#define INVALID_REALPIPEHANDLE INVALID_PIPEHANDLE
#endif

class TShellCommand::TImpl
   : public TAtomicRefCount<TShellCommand::TImpl> {
private:
    TPid Pid;
    TString Command;
    ylist<TString> Arguments;
    TString WorkDir;
    TShellCommand::ECommandStatus ExecutionStatus;
    TMaybe<int> ExitCode;
    TInputStream* InputStream;
    TOutputStream* OutputStream;
    TOutputStream* ErrorStream;
    TString CollectedOutput;
    TString CollectedError;
    TString InternalError;
    TThread* WatchThread;
    TMutex TerminateMutex;
    /// @todo: store const TShellCommandOptions, no need for so many vars
    bool TerminateFlag;
    bool ClearSignalMask;
    bool CloseAllFdsOnExec;
    bool AsyncMode;
    size_t PollDelayMs;
    bool UseShell;
    bool QuoteArguments;
    bool DetachSession;
    bool CloseStreams;
    TAtomic ShouldCloseInput;
    TShellCommandOptions::TUserOptions User;
    yhash<TString, TString> Environment;
    int Nice;

    struct TProcessInfo {
        TImpl* Parent;
        TRealPipeHandle InputFd;
        TRealPipeHandle OutputFd;
        TRealPipeHandle ErrorFd;
        TProcessInfo(TImpl* parent, REALPIPEHANDLE inputFd, REALPIPEHANDLE outputFd, REALPIPEHANDLE errorFd)
            : Parent(parent)
            , InputFd(inputFd)
            , OutputFd(outputFd)
            , ErrorFd(errorFd)
        {
        }
    };

    struct TPipes {
        TRealPipeHandle OutputPipeFd[2];
        TRealPipeHandle ErrorPipeFd[2];
        TRealPipeHandle InputPipeFd[2];
        // pipes are closed by automatic dtor
        void PrepareParents() {
            OutputPipeFd[1].Close();
            ErrorPipeFd[1].Close();
#if defined(_unix_)
            // not really needed, io is done via poll
            SetNonBlock(OutputPipeFd[0]);
            SetNonBlock(ErrorPipeFd[0]);
            if (InputPipeFd[1].IsOpen())
                SetNonBlock(InputPipeFd[1]);
#endif
            if (InputPipeFd[1].IsOpen())
                InputPipeFd[0].Close();
        }
        void ReleaseParents() {
            InputPipeFd[1].Release();
            OutputPipeFd[0].Release();
            ErrorPipeFd[0].Release();
        }
    };

private:
    TString GetQuotedCommand() const;
#if defined(_unix_)
    void OnFork(TPipes& pipes, sigset_t oldmask, char* const* argv, char* const* envp) const;
#else
    void StartProcess(TPipes& pipes);
#endif

public:
    inline TImpl(const TStringBuf cmd, const ylist<TString>& args, const TShellCommandOptions& options, const TString& workdir)
        : Pid(0)
        , Command(cmd.ToString())
        , Arguments(args)
        , WorkDir(workdir)
        , ExecutionStatus(SHELL_NONE)
        , InputStream(options.InputStream)
        , OutputStream(options.OutputStream)
        , ErrorStream(options.ErrorStream)
        , WatchThread(nullptr)
        , TerminateFlag(false)
        , ClearSignalMask(options.ClearSignalMask)
        , CloseAllFdsOnExec(options.CloseAllFdsOnExec)
        , AsyncMode(options.AsyncMode)
        , PollDelayMs(options.PollDelayMs)
        , UseShell(options.UseShell)
        , QuoteArguments(options.QuoteArguments)
        , DetachSession(options.DetachSession)
        , CloseStreams(options.CloseStreams)
        , ShouldCloseInput(options.ShouldCloseInput)
        , User(options.User)
        , Environment(options.Environment)
        , Nice(options.Nice)
    {
    }

    inline ~TImpl() {
        if (WatchThread) {
            with_lock (TerminateMutex) {
                TerminateFlag = true;
            }

            delete WatchThread;
        }

#if defined(_win_)
        if (Pid) {
            CloseHandle(Pid);
        }
#endif
    }

    inline void AppendArgument(const TStringBuf argument) {
        if (ExecutionStatus == SHELL_RUNNING) {
            ythrow yexception() << "You cannot change command parameters while process is running";
        }
        Arguments.push_back(argument.ToString());
    }

    inline const TString& GetOutput() const {
        if (ExecutionStatus == SHELL_RUNNING) {
            ythrow yexception() << "You cannot retrieve output while process is running.";
        }
        return CollectedOutput;
    }

    inline const TString& GetError() const {
        if (ExecutionStatus == SHELL_RUNNING) {
            ythrow yexception() << "You cannot retrieve output while process is running.";
        }
        return CollectedError;
    }

    inline const TString& GetInternalError() const {
        if (ExecutionStatus != SHELL_INTERNAL_ERROR) {
            ythrow yexception() << "Internal error hasn't occured so can't be retrieved.";
        }
        return InternalError;
    }

    inline ECommandStatus GetStatus() const {
        return ExecutionStatus;
    }

    inline TMaybe<int> GetExitCode() const {
        return ExitCode;
    }

    inline TProcessId GetPid() const {
#if defined(_win_)
        return GetProcessId(Pid);
#else
        return Pid;
#endif
    }

    // start child process
    void Run();

    inline void Terminate() {
        if (!!Pid && (ExecutionStatus == SHELL_RUNNING)) {
            bool ok =
#if defined(_unix_)
                kill(DetachSession ? -1 * Pid : Pid, SIGTERM) == 0;
            if (!ok && (errno == ESRCH) && DetachSession) {
                // this could fail when called before child proc completes setsid().
                ok = kill(Pid, SIGTERM) == 0;
                kill(-Pid, SIGTERM); // between a failed kill(-Pid) and a successful kill(Pid) a grandchild could have been spawned
            }
#else
                TerminateProcess(Pid, 1 /* exit code */);
#endif
            if (!ok)
                ythrow TSystemError() << "cannot terminate " << Pid;
        }
    }

    inline void Wait() {
        if (WatchThread)
            WatchThread->Join();
    }

    inline void CloseInput() {
        AtomicSet(ShouldCloseInput, true);
    }

    inline static bool TerminateIsRequired(void* processInfo) {
        TProcessInfo* pi = reinterpret_cast<TProcessInfo*>(processInfo);
        if (!pi->Parent->TerminateFlag) {
            return false;
        }
        pi->InputFd.Close();
        pi->ErrorFd.Close();
        pi->OutputFd.Close();

        if (pi->Parent->CloseStreams) {
            if (pi->Parent->ErrorStream)
                pi->Parent->ErrorStream->Finish();
            if (pi->Parent->OutputStream)
                pi->Parent->OutputStream->Finish();
        }

        delete pi;
        return true;
    }

    // interchange io while process is alive
    inline static void Communicate(TProcessInfo* pi);

    inline static void* WatchProcess(void* data) {
        TProcessInfo* pi = reinterpret_cast<TProcessInfo*>(data);
        Communicate(pi);
        return nullptr;
    }
};

#if defined(_win_)
void TShellCommand::TImpl::StartProcess(TShellCommand::TImpl::TPipes& pipes) {
    // Setup STARTUPINFO to redirect handles.
    STARTUPINFOW startup_info;
    ZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;

    if (!SetHandleInformation(pipes.OutputPipeFd[1], HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT) || !SetHandleInformation(pipes.ErrorPipeFd[1], HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
        ythrow TSystemError() << "cannot set handle info";
    if (InputStream)
        if (!SetHandleInformation(pipes.InputPipeFd[0], HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
            ythrow TSystemError() << "cannot set handle info";

    // A sockets do not work as std streams for some reason
    startup_info.hStdOutput = pipes.OutputPipeFd[1];
    startup_info.hStdError = pipes.ErrorPipeFd[1];
    startup_info.hStdInput = nullptr;
    if (InputStream)
        startup_info.hStdInput = pipes.InputPipeFd[0];

    PROCESS_INFORMATION process_info;
    // TString cmd = "cmd /U" + TUtf16String can be used to read unicode messages from cmd
    // /A - ansi charset /Q - echo off, /C - command, /Q - special quotes
    TString qcmd = GetQuotedCommand();
    TString cmd = UseShell ? "cmd /A /Q /S /C \"" + qcmd + "\"" : qcmd;
    // winapi can modify command text, copy it

    Y_ENSURE(+cmd < MAX_COMMAND_LINE, STRINGBUF("Command is too long"));
    TTempArray<wchar_t> cmdcopy(MAX_COMMAND_LINE);
    Copy(~cmd, ~cmd + +cmd, cmdcopy.Data());
    *(cmdcopy.Data() + +cmd) = 0;

    const wchar_t* cwd = NULL;
    std::wstring cwdBuff;
    if (+WorkDir) {
        cwdBuff = GetWString(~WorkDir);
        cwd = cwdBuff.c_str();
    }

    void* lpEnvironment = nullptr;
    TString env;
    if (!Environment.empty()) {
        for (auto e = Environment.begin(); e != Environment.end(); ++e) {
            env += e->first + '=' + e->second + '\0';
        }
        env += '\0';
        lpEnvironment = const_cast<char*>(~env);
    }

// disable messagebox (may be in debug too)
#ifndef NDEBUG
    SetErrorMode(GetErrorMode() | SEM_NOGPFAULTERRORBOX);
#endif
    BOOL res = 0;
    if (User.Name.empty() || GetUsername() == User.Name) {
        res = CreateProcessW(
            nullptr, // image name
            cmdcopy.Data(),
            nullptr,       // process security attributes
            nullptr,       // thread security attributes
            TRUE,          // inherit handles - needed for IO, CloseAllFdsOnExec not respected
            0,             // obscure creation flags
            lpEnvironment, // environment
            cwd,           // current directory
            &startup_info,
            &process_info);
    } else {
        res = CreateProcessWithLogonW(
            GetWString(~User.Name).c_str(),
            nullptr, // domain (if this parameter is NULL, the user name must be specified in UPN format)
            GetWString(~User.Password).c_str(),
            0,    // logon flags
            NULL, // image name
            cmdcopy.Data(),
            0,             // obscure creation flags
            lpEnvironment, // environment
            cwd,           // current directory
            &startup_info,
            &process_info);
    }

    if (!res) {
        ExecutionStatus = SHELL_ERROR;
        /// @todo: write to error stream if set
        TStringOutput out(CollectedError);
        out << "Process was not created: " << LastSystemErrorText() << " command text was: '" << GetAString(cmdcopy.Data()) << "'";
    }
    Pid = process_info.hProcess;
    CloseHandle(process_info.hThread);
    DBG(Cerr << "created process id " << Pid << " in dir: " << cwd << ", cmd: " << cmdcopy.Data() << Endl);
}
#endif

void ShellQuoteArg(TString& dst, TStringBuf argument) {
    dst.append("\"");
    TStringBuf l, r;
    while (argument.TrySplit('"', l, r)) {
        dst.append(l);
        dst.append("\\\"");
        argument = r;
    }
    dst.append(argument);
    dst.append("\"");
}

void ShellQuoteArgSp(TString& dst, TStringBuf argument) {
    dst.append(' ');
    ShellQuoteArg(dst, argument);
}

TString TShellCommand::TImpl::GetQuotedCommand() const {
    TString quoted = Command; /// @todo command itself should be quoted too
    for (const auto& argument : Arguments) {
        if (QuoteArguments) {
            ::ShellQuoteArgSp(quoted, argument);
        } else {
            quoted.append(" ").append(argument);
        }
    }
    return quoted;
}

#if defined(_unix_)
void TShellCommand::TImpl::OnFork(TPipes& pipes, sigset_t oldmask, char* const* argv, char* const* envp) const {
    try {
        if (DetachSession)
            setsid();

        // reset signal handlers from parent
        struct sigaction sa;
        sa.sa_handler = SIG_DFL;
        sa.sa_flags = 0;
        SigEmptySet(&sa.sa_mask);
        for (int i = 0; i < NSIG; ++i) {
            // some signals cannot be caught, so just ignore return value
            sigaction(i, &sa, nullptr);
        }
        if (ClearSignalMask) {
            SigEmptySet(&oldmask);
        }
        // clear / restore signal mask
        if (SigProcMask(SIG_SETMASK, &oldmask, nullptr) != 0) {
            ythrow TSystemError() << "Cannot " << (ClearSignalMask ? "clear" : "restore") << " signal mask in child";
        }

        pipes.OutputPipeFd[0].Close();
        pipes.ErrorPipeFd[0].Close();
        TFileHandle sIn(0);
        TFileHandle sOut(1);
        TFileHandle sErr(2);
        if (InputStream) {
            pipes.InputPipeFd[1].Close();
            TFileHandle sInNew(pipes.InputPipeFd[0]);
            sIn.LinkTo(sInNew);
            sIn.Release();
            sInNew.Release();
        } else {
            // do not close fd 0 - next open will return it and confuse all readers
            /// @todo in case of real need - reopen /dev/null
        }
        TFileHandle sOutNew(pipes.OutputPipeFd[1]);
        sOut.LinkTo(sOutNew);
        sOut.Release();
        sOutNew.Release();
        TFileHandle sErrNew(pipes.ErrorPipeFd[1]);
        sErr.LinkTo(sErrNew);
        sErr.Release();
        sErrNew.Release();

        if (+WorkDir)
            NFs::SetCurrentWorkingDirectory(WorkDir);

        if (CloseAllFdsOnExec) {
            for (int fd = NSystemInfo::MaxOpenFiles(); fd > STDERR_FILENO; --fd) {
                fcntl(fd, F_SETFD, FD_CLOEXEC);
            }
        }

        if (!User.Name.empty()) {
            ImpersonateUser(User.Name);
        }

        if (Nice) {
            Y_VERIFY(::Nice(Nice), "nice() failed(%s)", LastSystemErrorText());
        }

        if (envp == nullptr) {
            execvp(argv[0], argv);
        } else {
            execve(argv[0], argv, envp);
        }
        Cerr << "Process was not created: " << LastSystemErrorText() << Endl;
    } catch (const std::exception& error) {
        Cerr << "Process was not created: " << error.what() << Endl;
    } catch (...) {
        Cerr << "Process was not created: "
             << "unknown error" << Endl;
    }

    exit(-1);
}
#endif

void TShellCommand::TImpl::Run() {
    Y_ENSURE(ExecutionStatus != SHELL_RUNNING, STRINGBUF("Process is already running"));
    // Prepare I/O streams
    CollectedOutput.clear();
    CollectedError.clear();
    TPipes pipes;

    TRealPipeHandle::Pipe(pipes.OutputPipeFd[0], pipes.OutputPipeFd[1]);
    TRealPipeHandle::Pipe(pipes.ErrorPipeFd[0], pipes.ErrorPipeFd[1]);
    if (InputStream) {
        TRealPipeHandle::Pipe(pipes.InputPipeFd[0], pipes.InputPipeFd[1]);
    }

    ExecutionStatus = SHELL_RUNNING;

#if defined(_unix_)
    // block all signals to avoid signal handler race after fork()
    sigset_t oldmask, newmask;
    SigFillSet(&newmask);
    if (SigProcMask(SIG_SETMASK, &newmask, &oldmask) != 0) {
        ythrow TSystemError() << "Cannot block all signals in parent";
    }

    /* arguments holders */
    TString shellArg;
    yvector<char*> qargv;
    /*
      Following "const_cast"s are safe:
      http://pubs.opengroup.org/onlinepubs/9699919799/functions/exec.html
    */
    if (UseShell) {
        shellArg = GetQuotedCommand();
        qargv.reserve(4);
        qargv.push_back(const_cast<char*>("/bin/sh"));
        qargv.push_back(const_cast<char*>("-c"));
        // two args for 'sh -c -- ',
        // one for program name, and one for NULL at the end
        qargv.push_back(const_cast<char*>(~shellArg));
    } else {
        qargv.reserve(Arguments.size() + 2);
        qargv.push_back(const_cast<char*>(~Command));
        for (auto& i : Arguments) {
            qargv.push_back(const_cast<char*>(~i));
        }
    }

    qargv.push_back(nullptr);

    yvector<TString> envHolder;
    yvector<char*> envp;
    if (!Environment.empty()) {
        for (auto& env : Environment) {
            envHolder.emplace_back(env.first + '=' + env.second);
            envp.push_back(const_cast<char*>(~envHolder.back()));
        }
        envp.push_back(nullptr);
    }

    pid_t pid = fork();
    if (pid == -1) {
        ExecutionStatus = SHELL_ERROR;
        /// @todo check if pipes are still open
        ythrow TSystemError() << "Cannot fork";
    } else if (pid == 0) { // child
        if (envp.size() != 0) {
            OnFork(pipes, oldmask, qargv.data(), envp.data());
        } else {
            OnFork(pipes, oldmask, qargv.data(), nullptr);
        }
    } else { // parent
        // restore signal mask
        if (SigProcMask(SIG_SETMASK, &oldmask, nullptr) != 0) {
            ythrow TSystemError() << "Cannot restore signal mask in parent";
        }
    }
    Pid = pid;
#else
    StartProcess(pipes);
#endif
    pipes.PrepareParents();

    if (ExecutionStatus != SHELL_RUNNING)
        return;

    TProcessInfo* processInfo = new TProcessInfo(this,
                                                 pipes.InputPipeFd[1].Release(), pipes.OutputPipeFd[0].Release(), pipes.ErrorPipeFd[0].Release());
    if (AsyncMode) {
        WatchThread = new TThread(&TImpl::WatchProcess, processInfo);
        WatchThread->Start();
        /// @todo wait for child to start its process session (if options.Detach)
    } else {
        Communicate(processInfo);
    }
    pipes.ReleaseParents(); // not needed
}

void TShellCommand::TImpl::Communicate(TProcessInfo* pi) {
    THolder<TOutputStream> outputHolder;
    TOutputStream* output = pi->Parent->OutputStream;
    if (!output)
        outputHolder.Reset(output = new TStringOutput(pi->Parent->CollectedOutput));

    THolder<TOutputStream> errorHolder;
    TOutputStream* error = pi->Parent->ErrorStream;
    if (!error)
        errorHolder.Reset(error = new TStringOutput(pi->Parent->CollectedError));

    TInputStream*& input = pi->Parent->InputStream;

    try {
        TBuffer buffer(1024 * 1024);
        TBuffer inputBuffer(1024 * 1024);
        int bytes;
        int bytesToWrite = 0;
        char* bufPos = nullptr;

        TWaitResult waitPidResult;
        TExitStatus status = 0;

        while (true) {
            bool haveIn = false;
            bool haveOut = false;
            bool haveErr = false;

            {
                with_lock (pi->Parent->TerminateMutex) {
                    if (TerminateIsRequired(pi)) {
                        return;
                    }
                }

                waitPidResult =
#if defined(_unix_)
                    waitpid(pi->Parent->Pid, &status, WNOHANG);
#else
                    WaitForSingleObject(pi->Parent->Pid /* process_info.hProcess */, 0 /* ms */);
                Y_UNUSED(status);
#endif
                // DBG(Cerr << "wait result: " << waitPidResult << Endl);
                if (waitPidResult != WAIT_PROCEED)
                    break;
            }

            if (!input && pi->InputFd.IsOpen()) {
                DBG(Cerr << "closing input stream..." << Endl);
                pi->InputFd.Close();
            }
            if (!output && pi->OutputFd.IsOpen()) {
                DBG(Cerr << "closing output stream..." << Endl);
                pi->OutputFd.Close();
            }
            if (!error && pi->ErrorFd.IsOpen()) {
                DBG(Cerr << "closing error stream..." << Endl);
                pi->ErrorFd.Close();
            }

            if (!input && !output && !error)
                continue;

/// @todo factor out (poll + wfmo)
#if defined(_win_)
            HANDLE handles[3];
            int handle_count = 0;
            /// @todo test stdin
            if (input) {
                handles[handle_count++] = REALPIPEHANDLE(pi->InputFd);
            }
            if (output) {
                handles[handle_count++] = REALPIPEHANDLE(pi->OutputFd);
            }
            if (error) {
                handles[handle_count++] = REALPIPEHANDLE(pi->ErrorFd);
            }

            DWORD wait_result = WaitForMultipleObjects(handle_count, handles, FALSE, pi->Parent->PollDelayMs);
            HANDLE signaled_handle = nullptr;
            DBG(Cerr << "wfmo result: " << wait_result << Endl);
            if (wait_result >= WAIT_OBJECT_0 && wait_result < WAIT_OBJECT_0 + handle_count) {
                signaled_handle = handles[wait_result - WAIT_OBJECT_0];
            } else if (wait_result == WAIT_FAILED) {
                ythrow TSystemError() << "WaitForMultipleObjects failed";
            } else {
                ythrow TSystemError() << "WaitForMultipleObjects: Unexpected return code: " << wait_result;
            }
            if (signaled_handle == REALPIPEHANDLE(pi->OutputFd))
                haveOut = true;
            else if (signaled_handle == REALPIPEHANDLE(pi->ErrorFd))
                haveErr = true;
            else if (signaled_handle == REALPIPEHANDLE(pi->InputFd))
                haveIn = true;

#elif defined(_unix_)
            struct pollfd fds[] = {
                {REALPIPEHANDLE(pi->InputFd), POLLOUT, 0},
                {REALPIPEHANDLE(pi->OutputFd), POLLIN, 0},
                {REALPIPEHANDLE(pi->ErrorFd), POLLIN, 0}};
            int res;

            if (!input)
                fds[0].events = 0;
            if (!output)
                fds[1].events = 0;
            if (!error)
                fds[2].events = 0;

            res = PollD(fds, 3, TInstant::Now() + TDuration::MilliSeconds(pi->Parent->PollDelayMs));
            // DBG(Cerr << "poll result: " << res << Endl);
            if (-res == ETIMEDOUT || res == 0) {
                // DBG(Cerr << "poll again..." << Endl);
                continue;
            }
            if (res < 0)
                ythrow yexception() << "poll failed: " << LastSystemErrorText();

            if ((fds[1].revents & POLLIN) == POLLIN)
                haveOut = true;
            else if (fds[1].revents & (POLLERR | POLLHUP))
                output = nullptr;

            if ((fds[2].revents & POLLIN) == POLLIN)
                haveErr = true;
            else if (fds[2].revents & (POLLERR | POLLHUP))
                error = nullptr;

            if (input && ((fds[0].revents & POLLOUT) == POLLOUT))
                haveIn = true;
#endif
            if (haveOut) {
                bytes = pi->OutputFd.Read(buffer.Data(), buffer.Capacity());
                DBG(Cerr << "transferred " << bytes << " bytes of output" << Endl);
                if (bytes > 0)
                    output->Write(buffer.Data(), bytes);
                else
                    output = nullptr;
            }
            if (haveErr) {
                bytes = pi->ErrorFd.Read(buffer.Data(), buffer.Capacity());
                DBG(Cerr << "transferred " << bytes << " bytes of error" << Endl);
                if (bytes > 0)
                    error->Write(buffer.Data(), bytes);
                else
                    error = nullptr;
            }

            if (haveIn) {
                if (!bytesToWrite) {
                    bytesToWrite = input->Read(inputBuffer.Data(), inputBuffer.Capacity());
                    if (bytesToWrite == 0) {
                        if (AtomicGet(pi->Parent->ShouldCloseInput)) {
                            input = nullptr;
                        }
                        continue;
                    }
                    bufPos = inputBuffer.Data();
                }

                bytes = pi->InputFd.Write(bufPos, bytesToWrite);
                if (bytes > 0) {
                    bytesToWrite -= bytes;
                    bufPos += bytes;
                } else {
                    input = nullptr;
                }

                DBG(Cerr << "transferred " << bytes << " bytes of input" << Endl);
            }
        }
        DBG(Cerr << "process finished" << Endl);

        // Now let's read remaining stdout/stderr
        while (output && (bytes = pi->OutputFd.Read(buffer.Data(), buffer.Capacity())) > 0) {
            DBG(Cerr << bytes << " more bytes of output: " << Endl);
            output->Write(buffer.Data(), bytes);
        }
        while (error && (bytes = pi->ErrorFd.Read(buffer.Data(), buffer.Capacity())) > 0) {
            DBG(Cerr << bytes << " more bytes of error" << Endl);
            error->Write(buffer.Data(), bytes);
        }
        // What's the reason of process exit
        bool cleanExit = false;
        TMaybe<int> processExitCode;
#if defined(_unix_)
        processExitCode = WEXITSTATUS(status);
        if (WIFEXITED(status) && processExitCode == 0)
            cleanExit = true;
#else
        if (waitPidResult == WAIT_OBJECT_0) {
            DWORD exitCode = STILL_ACTIVE;
            if (!GetExitCodeProcess(pi->Parent->Pid, &exitCode)) {
                ythrow yexception() << "GetExitCodeProcess: " << LastSystemErrorText();
            }
            if (exitCode == 0)
                cleanExit = true;
            processExitCode = static_cast<int>(exitCode);
            DBG(Cerr << "exit code: " << exitCode << Endl);
        }
#endif
        pi->Parent->ExitCode = processExitCode;
        if (cleanExit) {
            pi->Parent->ExecutionStatus = SHELL_FINISHED;
        } else {
            pi->Parent->ExecutionStatus = SHELL_ERROR;
        }
    } catch (const yexception& e) {
        // Some error in watch occured, set result to error
        pi->Parent->ExecutionStatus = SHELL_INTERNAL_ERROR;
        pi->Parent->InternalError = e.what();
        if (input)
            pi->InputFd.Close();
        Cdbg << "shell command internal error: " << pi->Parent->InternalError << Endl;
    }
    // Now we can safely delete process info struct and other data
    pi->Parent->TerminateFlag = true;
    TerminateIsRequired(pi);
}

TShellCommand::TShellCommand(const TStringBuf cmd, const ylist<TString>& args, const TShellCommandOptions& options,
                             const TString& workdir)
    : Impl(new TImpl(cmd, args, options, workdir))
{
}

TShellCommand::TShellCommand(const TStringBuf cmd, const TShellCommandOptions& options, const TString& workdir)
    : Impl(new TImpl(cmd, ylist<TString>(), options, workdir))
{
}

TShellCommand::~TShellCommand() = default;

TShellCommand& TShellCommand::operator<<(const TStringBuf argument) {
    Impl->AppendArgument(argument);
    return *this;
}

const TString& TShellCommand::GetOutput() const {
    return Impl->GetOutput();
}

const TString& TShellCommand::GetError() const {
    return Impl->GetError();
}

const TString& TShellCommand::GetInternalError() const {
    return Impl->GetInternalError();
}

TShellCommand::ECommandStatus TShellCommand::GetStatus() const {
    return Impl->GetStatus();
}

TMaybe<int> TShellCommand::GetExitCode() const {
    return Impl->GetExitCode();
}

TProcessId TShellCommand::GetPid() const {
    return Impl->GetPid();
}

TShellCommand& TShellCommand::Run() {
    Impl->Run();
    return *this;
}

TShellCommand& TShellCommand::Terminate() {
    Impl->Terminate();
    return *this;
}

TShellCommand& TShellCommand::Wait() {
    Impl->Wait();
    return *this;
}

TShellCommand& TShellCommand::CloseInput() {
    Impl->CloseInput();
    return *this;
}
