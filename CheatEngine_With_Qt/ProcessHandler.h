#pragma once
#include <Windows.h>
#include "Cheat_Engine_Core/NewKernelHandler.h"



enum TSystemArchitecture
{
    archX86 = 0,
    archArm = 1
};

enum TOperatingsystemABI
{
    abiWindows = 0, 
    abiSystemV = 1
};

class ProcessHandler
{
private:
    NewKernelHandler* newKernelHandler = new NewKernelHandler();


    bool fIs64bit;
    bool fIsAndroid;
    HANDLE fProcessHandle;
    int fPointerSize;
    TSystemArchitecture fSystemArchitecture;
    TOperatingsystemABI fOSABI;
    int fHexDigitPreference;

    void setIs64bit(bool state);
    void setProcessHandle(HANDLE processHandle);

public:
    DWORD processId;

    void Open();
    bool isNetwork();
    void overridePointerSize(int newSize);



    // 通过访问器和修改器方法模拟属性 
    bool getIs64Bit() const { return fIs64bit; }

    bool getIsAndroid() const { return fIsAndroid; }

    int getPointerSize() const { return fPointerSize; }

    HANDLE getProcessHandle() const { return fProcessHandle; }
 

    TSystemArchitecture getSystemArchitecture() const { return fSystemArchitecture; }
    void setSystemArchitecture(TSystemArchitecture arch) { fSystemArchitecture = arch; }

    TOperatingsystemABI getOSABI() const { return fOSABI; }

    int getHexDigitPreference() const { return fHexDigitPreference; }



};

