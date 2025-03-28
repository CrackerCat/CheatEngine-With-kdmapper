#include "ProcessHandler.h"


void ProcessHandler::setIs64bit(bool state)
{
	fIs64bit = state;
	if (state)
	{
		fPointerSize = 8;
	}
	else
	{
		fPointerSize = 4;
	}
	fHexDigitPreference = fPointerSize * 2;
}

void ProcessHandler::setProcessHandle(HANDLE processHandle)
{
	if (fProcessHandle != NULL && fProcessHandle != GetCurrentProcess() && processHandle != GetCurrentProcess)
	{
		try
		{
			CloseHandle(fProcessHandle);
		}
		catch (const std::exception&)
		{
			//debuger issue
		}
		fProcessHandle = NULL;
	}

	fProcessHandle = processHandle;

	fIsAndroid = false;
	fSystemArchitecture = archX86;
	fOSABI = abiWindows;
	setIs64bit(newKernelHandler->Is64BitProcess(fProcessHandle));


}

void ProcessHandler::overridePointerSize(int newSize)
{
   fPointerSize = newSize;
}




