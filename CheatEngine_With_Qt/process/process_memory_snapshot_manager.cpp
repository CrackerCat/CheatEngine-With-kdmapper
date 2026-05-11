#include "process\process_memory_snapshot_manager.h"
#include "process\process_manager.h"
#include "utils\TempPathManager.h"
#include "Factory\process_memory_snapshot_factory.h" // 新增工厂头文件
#include <filesystem>
#include <fstream>

ProcessMemorySnapshotManager::ProcessMemorySnapshotManager(MemoryBackend backend)
	: m_backend(backend)
	, m_first(nullptr)
	, m_prev(nullptr)

{
}

std::shared_ptr<IProcessMemorySnapshot> ProcessMemorySnapshotManager::createSnapshot(const std::vector<MemoryRegion>& regions) {
	// 1. 生成唯一的临时文件名
	std::string path = (std::filesystem::path(TempPathManager::getWorkDir()) /
		("snapshot_" + std::to_string(GetTickCount64()) + ".bin")).string();

	std::ofstream outFile(path, std::ios::binary);
	if (!outFile) return nullptr;

	std::map<uint64_t, size_t> index;
	size_t currentFileOffset = 0;

	// 2. 获取当前的内存读取器 (解耦的关键)
	auto accessor = ProcessManager::instance().memory();
	if (!accessor) return nullptr;

	// 3. 遍历区域并写入文件
	for (const auto& reg : regions) {
		std::vector<uint8_t> buffer(reg.size);

		// 使用 IMemoryAccessor 读取内存
		// 假设您的 IMemoryAccessor 接口是 read(uint64_t addr, void* buf, size_t size)
		if (accessor->read(reg.base, buffer.data(), reg.size)) {
			outFile.write(reinterpret_cast<const char*>(buffer.data()), reg.size);

			// 记录索引：内存虚拟地址 -> 镜像文件中的偏移位置
			index[reg.base] = currentFileOffset;
			currentFileOffset += reg.size;
		}
	}

	outFile.close();

	// 4. 返回封装好的快照实体
	return ProcessMemorySnapshotFactory::create(m_backend, path, std::move(index));
}


void ProcessMemorySnapshotManager::clear() {
	// 处理第一个快照：先 reset 释放 Win32 句柄，再执行文件删除
	if (m_first) {
		std::string path = m_first->path();
		m_first.reset(); // 此时会触发 Win32ProcessMemorySnapshot 的析构，调用 CloseHandle

		std::error_code ec;
		std::filesystem::remove(path, ec); // 使用 error_code 版本避免异常抛出
	}

	// 处理上一个快照
	if (m_prev) {
		std::string path = m_prev->path();
		m_prev.reset();

		std::error_code ec;
		std::filesystem::remove(path, ec);
	}
}