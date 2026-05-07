# Cheat-Engine-with-kdmapper
1、Attempt to rewrite the Cheat Engine Windows framework using Qt,may its a big project that take a long time to finish

2、DBK Driver incremental development based on kdmapper (finished)

If you don't want to wait for my rewrite project to be completed, please replace the DBK32functions.pas file with the official CE project and recompile it 



Chinese commemt:

1、本项目基于两个著名黑客项目增量开发 通过kdmapper手动映射DBK驱动，并重构DBK原DeviceIoControl方法与Cheat Engine的通讯机制，绕过微软签名

2、尝试使用Qt框架，重构CE在Windows平台 amd64 处理器基本功能，本人讨厌pascal，还有lua、java等编程语言，因lazarus 的TForm 特征被各大游戏封禁，CheatEngine作为pascal编程语言相关特征已经无法满足游戏反作弊的要求， 这个重构项目会持续非常久，欢迎大家一起开发，目前重构了界面功能，后续
会逐渐增加原pascal编程语言的其他功能，PS: 日积月累


我的bilibili主页 https://space.bilibili.com/131403708

version:2026/5/3


Qt重构初版 Cheat Engine  实现了首次扫描、下一次扫描、核心磁盘缓存、扫描引擎、内存枚举

AI 辅助开发和遇到的问题链接

https://chat.deepseek.com/share/dwx8by5y1aop9tcp83

https://gemini.google.com/share/8460f0ffaa83

https://chatgpt.com/g/g-p-69f130f821608191b7f8898d76d6cf87-cheat-engine/project


特点

1、教科书级别，现代工程级别软件工程架构，Scan层标准输入输出流，高内聚、低耦合架构，极高的代码可读性，可维护性

2、海量数据、高并发、线程安全，内存优化策略、多线程局部TLS，完美的UI异步架构再多条目的地址内存拷贝也不会导致UI卡顿

3、自适应磁盘扫描缓冲，即使再多的地址条目也不会大量占用内存

4、反作弊安全，规避官方版Cheat Engine 的lazarus 架构TForm、和一些pascal语言库，应用层不会出问题

5、工程级别的架构，应用层对底层实现透明，可完美扩展，DBK、WIN_API、DBVM

6、强大的反汇编引擎，使用成熟的库来实现反汇编，解决原版CE项目自写反汇编引擎混乱，代码可读性差问题

7、集成kdmapper_dll，自动加载映射 DBK，无需官方版Cheat Engine需要各种注册表Key才能注册驱动的痛点



项目开发环境


Visual Studio、Qt6


C++代码编译模式MDd、和MD (注意不要使用 MTd、MT的编译模式，会和Qt库冲突，导致异常崩溃)




<img width="851" height="876" alt="image" src="https://github.com/user-attachments/assets/c6c38e76-0bef-4efe-a925-7262ba8acb13" />


扫描架构理解




驱动层DBK

本项目的内容和目的，是在基于kdmmaper和Cheat Engine的开源项目，手动映射Cheat Engine的DBKKernel驱动，实现DBK驱动模块隐藏，与应用层Cheat Engine应用通讯，DBVM加载、内存搜索、软件调试等、官方版CE全部功能

1、绕过微软驱动强制签名问题，无论官方，还是自编译Cheat Engine项目都无法绕过驱动签名问题，本项目核心解决如此


2、解决Cheat Engine DBKKernal驱动特征（如注册表、服务名，驱动模块）等特征，容易被特征识别，在内核中真正无模块或内存特征运行

3、DBKKernal源码级别重构，精简官方DBK项目中多出逻辑Bug，和结构混乱，去除冗余项目代码，补全未完成的开发功能，更新微软驱动开发库，排除编译问题

4、移植kdmapper项目在应用层实现的内核模块遍历，和内核特征码匹配到内核层，在内核层实现内核模块遍历，特征匹配

5、正常实现DBVM所有功能，调试器，内存扫描，VT调试读写




原Cheat Engine DBKKernl通讯机制限制



1、Cheat Engine 的常规通讯方式 为调用Windows应用原生API读写内存，和其他的一些功能，这些应用层api会被安全类软件或是游戏反作弊引擎拦截，无法实现所需功能

2、第二种则是通过DBK驱动，向DBK内核驱动发送通讯控制码,和输入输出缓存，实现通讯并实现内核CE功能，但需要DBK驱动加载成功后才能实现，现有驱动签名均无法在最新版本Windows上检查通过

3、第三种通过DBVM设备，即一个内核VT虚拟机设备来实现通讯，如果DBVM已经加载，则DBK的相关控制派遣函数会被通过DBVM转发到DBK来实现功能，但是加载DBVM设备需要通过DBK 去启动，那么DBK就必须获得微软的签名，因此仍然无法解决驱动签名问题


两种实现方式:

1、通过Hook内核驱动设备FastIoDeviceControl,，设备名称已在头文件中定义，实现隐蔽通讯，本项目示例为HookNull设备FastIoDeviceControl

2、通过ntoskrnl.exe的.data数据段来实现调用内核未导出函数，实现隐式通讯，目前已实现找到未导出函数地址，实现原理为，移植kdmmaper中C++方式应用层查找内核模块与特征码，转为Windows内核C语言模式的内核模块与特征码查找


dbk无签名加载机制的实现


1、DBK驱动DriverEntry中定义了应用层传递的四个参数、设备名、服务名、进程名、 线程名，通过注册表读写机制确认是否为Cheat Engine应用加载，否则加载失败，本项目去除了这一加载条件，在原加载机制和dbvm加载机制，中添加了第三种加载机制，即手动无驱动对象加载，通过kdmapper传递入口参数，识别加载方式，并实现驱动可行初始化操作

2、应用层加载和通讯控制重构，应用层的需要先加载服务，再写入注册表4个配置项和驱动对应后才实现加载成功，本项目去除了所有应用层对注册表和控制服务的机制，和其他非Io通讯判断DBK成功加载特征，在原有的2中DeviceIoControl上添加了第三种通过Null设备的通讯，传递输入输出buffer和控制码

3、驱动卸载添加，驱动卸载不再通过系统服务控制，直接通过设备Io加卸载控制码实现，定义新操作码实现驱动卸载

4、内存隐藏机制，通过kdmmaper实现的加载独立Indepent内存机制实现无内存特征或PAGE tag标志



项目调试心得
1、调试环境搭建，采取网络调试模式，其效率高于串口调试，虚拟机开启调试命令为
bcdedit /debug on
bcdedit /dbgsettings NET HOSTIP:<物理机ip地址> PORT:55555  KEY:zuoyuan.own.safe.key

后续在Visual Studio中配置调试设备



2、一般我们有两种调试模式，一种为kdmapper加载的方式，一种为服务加载的方式，服务加载的方式是有符号和源码路径的，调试起来清晰可见，而kdmapper映射
的驱动，就需要我们手动load符号， 一般方法为，首先关闭kdmapper 的加载驱动时抹除PE文件头，在Kdmapper加载时会返回驱动加载地址的imagebase，记录这个
地址然后在windbg使用 .reload /i DBK_Tiny.sys=<image_base>方式对某个地址手动加载符号和源码路径，这种方式可以kdmapper映射的无模块驱动排除未知错误，
非常实用




项目开发环境搭建心得


1、使用Visual Stdio 2022安装库

2、下载SDK的镜像文件即.iso的那个，方便重装操作系统后快速重建项目开发环境

3、WDK安装后也选择下载缓存，最好不是直接网络安装

4、最重要的一步，如果这些安装好后Visual Studio仍然没有出现驱动开发选项，请在Visual Studio installer中对IDE修改->添加单个组件->搜索WDK->重启IDE，可重建环境


Window内核项目通讯心得


1、深入理解Windows，DeviceIoControl在三环和零环的应用，结构和参数传递

2、内核调试和内核模块、理解各种地址转换、三环内存和零环内存机制，内存管理，如何重构原3环数据包，即控制码，和输入缓冲区，重构IRP通讯











