#pragma once
//#include <string>
#include <wtypes.h>



class dbk32functions {
public:

	dbk32functions();
	~dbk32functions();



	HANDLE hdevice;

	void DBK32Initialize();

	BOOL  GetCR3(HANDLE hProcess,SIZE_T* CR3);
	DWORD GetThreadsProcessOffset();

};