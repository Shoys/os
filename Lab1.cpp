#include <iostream>
#include <vector>
#include <string>
#include <filesystem>

#ifdef _WIN32
#include <Windows.h>
#else
#include <cstdio>
#include <unistd.h>
#include <boost/process.hpp>
#include <sys/resource.h>
namespace bp = boost::process;
#endif
namespace fs = std::filesystem;

// Function to handle Ctrl-C
#ifdef _WIN32
HANDLE gitProcessId = NULL;
bool CtrlCHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT) {
        if (gitProcessId != NULL) {
            // Terminate the git process if it's running
            TerminateProcess(gitProcessId, 1);
            CloseHandle(gitProcessId);
        }
    }
    return true;
}
#else
#include <sys/wait.h>

pid_t gitProcessId = -1;

bool CtrlCHandler(int sig) {
    if (sig == SIGINT) {
        if (gitProcessId != -1) {
            // Terminate the git process if it's running
            kill(gitProcessId, SIGKILL); // or SIGTERM if we know it's really git, it handles SIGTERM better
            waitpid(gitProcessId, NULL, 0);
        }
    }
    return true;
}
#endif

// Function to run a command and capture its output (stdout and stderr)
std::string RunCommand(const std::string& command) {
    std::string result;

#ifdef _WIN32
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;
    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &saAttr, 0)) {
        throw std::runtime_error("CreatePipe() failed!");
    }

    STARTUPINFOW si; // Use STARTUPINFOW for wide-character strings
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdError = hWritePipe;
    si.hStdOutput = hWritePipe;
    si.dwFlags |= STARTF_USESTDHANDLES;

    std::wstring wcommand(command.begin(), command.end());
    if (!CreateProcessW(NULL, const_cast<LPWSTR>(wcommand.c_str()), NULL, NULL, TRUE, NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi)) { // Use CreateProcessW
        throw std::runtime_error("CreateProcess() failed!");
    }

    CloseHandle(hWritePipe);
    char buffer[128];

    while (true) {
        DWORD bytesRead;
        if (!ReadFile(hReadPipe, buffer, sizeof(buffer), &bytesRead, NULL) || bytesRead == 0) {
            break;
        }
        result += std::string(buffer, bytesRead);
    }

    CloseHandle(hReadPipe);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

#else
    boost::asio::io_context ios;
    std::vector<char> buffer(4096);
    bp::child gitProcess(bp::search_path("git"), "fetch", "origin", "--verbose", bp::std_err > boost::asio::buffer(buffer), ios);
    gitProcessId = gitProcess.id();
    // Set priority
    setpriority(PRIO_PROCESS, gitProcessId, 19);
    ios.run();
    gitProcess.wait();
    int status = gitProcess.exit_code();
    if (status != 0) {
        std::cerr << "Command failed with status: " << status << std::endl;
    }
    result = std::string(buffer.begin(), buffer.end());
#endif

    return result;
}

// Function to check if a Git repository is updated
bool IsGitRepositoryUpdated(const fs::path& repoPath) {
    fs::current_path(repoPath);
    std::string command = "git fetch --verbose origin"; // May want to use global path to make sure it's really git, also check write permissions to make sure user can't edit a file that may be executed by this script as root
    std::string output = RunCommand(command);
    // Check if the output contains "up to date"
    return output.find("up to date") == std::string::npos;
}

int main() {
    // Set Ctrl-C handler
#ifdef _WIN32
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlCHandler, TRUE);
#else
    signal(SIGINT, reinterpret_cast<__sighandler_t>(CtrlCHandler));
#endif

    std::vector<fs::path> updatedRepositories;

    // Get the current directory
    fs::path currentDir = fs::current_path();

    for (const fs::path& entry : fs::directory_iterator(currentDir)) {
        if (fs::is_directory(entry) && fs::exists(entry / ".git")) {
            if (IsGitRepositoryUpdated(entry)) {
                updatedRepositories.push_back(entry);
            }
        }
    }

    // Display updated repositories
    if (updatedRepositories.empty()) {
        std::cout << "No updated repositories found." << std::endl;
    }
    else {
        std::cout << "Updated repositories:" << std::endl;
        for (const auto& repo : updatedRepositories) {
            std::cout << repo << std::endl;
        }
    }

    return 0;
}
