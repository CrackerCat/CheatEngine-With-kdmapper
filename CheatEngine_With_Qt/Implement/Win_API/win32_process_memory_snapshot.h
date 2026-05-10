#pragma once
#include "iprocess_memory_snapshot.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX 
#include <windows.h>
#endif

class Win32ProcessMemorySnapshot : public IProcessMemorySnapshot {
public:
	Win32ProcessMemorySnapshot(const std::string& path, std::map<uint64_t, size_t> index);
	~Win32ProcessMemorySnapshot() override;

	bool readData(uint64_t address, uint8_t* buffer, size_t size) const override;
	const std::string& path() const override { return m_path; }
	const std::map<uint64_t, size_t>& index() const override { return m_index; }

private:
	std::string m_path;
	std::map<uint64_t, size_t> m_index;

	HANDLE m_hFile = INVALID_HANDLE_VALUE;
	HANDLE m_hMapping = nullptr;
	uint8_t* m_pBuffer = nullptr;
	size_t m_fileSize = 0;

	void initMapping();
};