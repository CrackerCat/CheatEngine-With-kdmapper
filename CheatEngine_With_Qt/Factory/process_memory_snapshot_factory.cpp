#include "Factory\process_memory_snapshot_factory.h"
#include "Implement\Win_API\win32_process_memory_snapshot.h"

std::unique_ptr<IProcessMemorySnapshot> ProcessMemorySnapshotFactory::create(MemoryBackend type, const std::string& path, std::map<uint64_t, size_t> index)
{
	switch (type)
	{
	case MemoryBackend::Win32:
		return std::make_unique<Win32ProcessMemorySnapshot>(path, std::move(index));
	default:
		return nullptr;
	}
}