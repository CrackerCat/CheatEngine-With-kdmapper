// TempPathManager.h
#pragma once

// 1. 必须在包含 windows.h 之前定义，防止 min/max 宏污染
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <string>
#include <filesystem>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <signal.h>
#endif

class TempPathManager {
public:
    // 使用 inline 确保在多个 .cpp 中包含时不会报重复定义错误
    static inline std::string getWorkDir() {
        static std::string path = "";
        if (path.empty()) {
            // 获取系统临时目录下的专用子目录
            auto tempBase = std::filesystem::temp_directory_path() / "MyScanApp_Data";
            auto pidDir = tempBase / std::to_string(getCurrentPid());

            std::error_code ec;
            std::filesystem::create_directories(pidDir, ec);
            path = pidDir.string();
        }
        return path;
    }

    static inline void cleanupOrphanedDirs() {
        auto tempBase = std::filesystem::temp_directory_path() / "MyScanApp_Data";
        if (!std::filesystem::exists(tempBase)) return;

        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(tempBase, ec)) {
            if (entry.is_directory()) {
                try {
                    uint32_t pid = std::stoul(entry.path().filename().string());
                    if (!isProcessRunning(pid)) {
                        std::filesystem::remove_all(entry.path(), ec);
                    }
                }
                catch (...) {
                    // 忽略非数字文件夹
                }
            }
        }
    }

private:
    static inline uint32_t getCurrentPid() {
#ifdef _WIN32
        return GetCurrentProcessId();
#else
        return getpid();
#endif
    }

    static inline bool isProcessRunning(uint32_t pid) {
#ifdef _WIN32
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process) {
            CloseHandle(process);
            return true;
        }
        return false;
#else
        return kill(pid, 0) == 0;
#endif
    }
};