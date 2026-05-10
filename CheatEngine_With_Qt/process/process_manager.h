#pragma once
#include <memory>
#include <string>
#include <vector>
#include <mutex> 
#include <shared_mutex>

#include "process_info.h"
#include "module_info.h"
#include "imemory_accessor.h"
#include "imodule_enumerator.h"
#include "imemory_region_enumerator.h"
#include "iprocess_enumerator.h"
#include "process_memory_snapshot_manager.h"

#include "memory_accessor_factory.h"
#include "scan_data_stream_define.h"

class ProcessManager
{
public:
	static ProcessManager& instance();

	// --- 进程生命周期管理 ---
	bool attach(const ProcessInfo& process);
	void detach();
	bool isProcessAlive() const;
	uint32_t attachedPid() const { return m_pid; }

	// --- 内存访问网关 (Scan模块核心依赖) ---
	std::shared_ptr<IMemoryAccessor> memory() { return m_accessor; }


	std::vector<MemoryRegion> getMemoryRegions();
	std::vector<MemoryRegion> getMemoryRegions(const ScanRequest& req);

	std::vector<ProcessInfo> getProcesses();

	// --- 地址与模块工具 ---
	bool resolveAddress(uint64_t addr, std::string& outDisplay, bool& isBase) const; // 地址解析：返回模块表示文本与是否为基址
	const std::vector<ModuleInfo>& modules() const { std::shared_lock lock(m_modulesMutex); return m_modules; } // 模块列表
	const ModuleInfo* getModuleByName(const std::string& name) const;


	// --- 后端配置 ---
	void setMemoryBackend(MemoryBackend type);   // 切换后端类型
	MemoryBackend currentBackend() const { return m_backend; }

	ProcessMemorySnapshotManager* getProcessMemorySnapshotManager() { return m_processMemorySnapshotManager.get(); }

private:
	ProcessManager() = default;
	~ProcessManager() = default;


	// 禁用拷贝
	ProcessManager(const ProcessManager&) = delete;
	ProcessManager& operator=(const ProcessManager&) = delete;

	IMemoryAccessor* memory_naked();    // 后台任务使用

	void initialize();
	void updateModules();
	void createBackends();  // 统一创建内存访问器和模块枚举器


	// 核心组件（通过工厂创建，实现依赖倒置）
	std::shared_ptr<IMemoryAccessor> m_accessor;
	std::unique_ptr<IModuleEnumerator> m_moduleEnumerator;
	std::unique_ptr<IMemoryRegionEnumerator> m_regionEnumerator;
	std::unique_ptr<IProcessEnumerator> m_processEnumerator;
	std::unique_ptr<ProcessMemorySnapshotManager> m_processMemorySnapshotManager;

	uint32_t m_pid = 0;
	std::vector<ModuleInfo> m_modules;
	MemoryBackend m_backend = MemoryBackend::Win32;
	std::once_flag m_initFlag;

	mutable std::shared_mutex m_modulesMutex;

};