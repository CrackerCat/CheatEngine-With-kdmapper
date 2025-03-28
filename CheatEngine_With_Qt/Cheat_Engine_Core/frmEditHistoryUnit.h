#pragma once
#ifndef frmEditHistoryUnit_H
#define frmEditHistoryUnit_H


#include <cstdint>



class TWriteLogEntry
{
public:
	int id;
	uintptr_t address; // Changed ptruint to uintptr_t
	size_t originalsize;
	uint8_t* originalbytes;
	size_t newsize;
	uint8_t* newbytes;

	bool logWrites = false;

	TWriteLogEntry();
	~TWriteLogEntry();
};

void addWriteLogEntryToList(TWriteLogEntry* wle);



#endif // !frmEditHistoryUnit_H