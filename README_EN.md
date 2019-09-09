# Loli Profiler

Lightweight Opensource profiLing Instrument

![](res/images/macos.png)

**Please Note: this project is still in early stage**

## Features

- Can profiler all debuggable apps (No limit on rooted devices)
- Collect any so library within your apk
- Hooked memory functions: malloc、calloc、realloc、memalign、free
- Translates function address to human readable format
- Filtering persistent memory allocations
- Query smaps info to get an memory overview of different so or modules
- Combine smaps info with memory allocation records to produce memory fragmentation map
- Take screen-shot every 5 second
- Transfer memory related function's call-stack in real-time (by TCP Socket)
- Network packet is compressed using lz4 to speed-up sending & receiving process
- Fast and portable (Developed in c++ and QT framework)
- Support Windows 10 & Max OSX (Mojave+)

## Intro

Install debuggable apk to your mobile device. Open LoliProfiler, select Python's path, text your app's name com.compnay.app, then press launch and you're done. After capturing data for a while, press Stop Capture to stop. After that, LoliProfiler will analyze received data and show those not freed memory allocations in Stacktrace tab. Normally the stack trace data includes function addresses, you will need to provide addr2line's path by clicking Addr2line button and select the correct executable in your ndk path. Then click Load Symblos to select the correct symblo for target .so library, then those address will be translated to human readable format.

![](res/images/screenshot.gif)

For engines like Unity or UE4, we can use below methods to force them to use malloc instead of internal memory pool. 

### Unity

```java
protected String updateUnityCommandLineArguments(String cmdLine) {
    return "-systemallocator";
}
```

### UE4

```c++
FMalloc* FAndroidPlatformMemory::BaseAllocator() {
#if USE_MALLOC
    return new FMallocAnsi();
#else
    return new FMallocBinned(MemoryConstants.PageSize, MemoryLimit);
#endif
}
```

## Plan

**Future plans**

* better user experience and improve performance
* implement better memory fragmentation viewer 
* and more ... 

## Inside

I use JDWP (Java Debug Wire Protocol) to inject dynamic library to target application.

And i initialize the profile functionalities in dynamic library's JNI_OnLoad function (Start TCP Server, start monitor threads etc).

I choose PLT Hook, not only because it has stable & production level implementation, but also for it's simplicity. And PLT Hook doesn't have the crash issue with gcc compiler when unwinding callstacks. The only problem is we can hook a library only when it's loaded.

I managed to read proc/self/maps file repeatedly to detect if there's any target so library is loaded to the application, if so i'll hook the newly loaded library automatically. This way we can hook any dynamic library in an application no matter when it will be loaded.

## Building

**Requirments**

* QT 5 or higher
* QtCharts plugin installed
* QT Creater 4.8 or higher
* C++11 Compiler
* Android NDK r16b or higher (If you want to build android plugin by hand)

## Links

* Prebuild Binaries https://git.code.oa.com/xinhou/loli_profiler/wikis/home
* FAQ in Chinese https://git.code.oa.com/xinhou/loli_profiler/wikis/faq
* Manual in Chinese https://git.code.oa.com/xinhou/loli_profiler/wikis/tutorial
* KM Intro http://km.oa.com/articles/show/408991
* xHook https://github.com/iqiyi/xHook
* JDWP Library https://koz.io/library-injection-for-debuggable-android-apps/
* App Icon https://www.flaticon.com/authors/smashicons
