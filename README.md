# Loli Profiler

Lightweight Opensource profiLing Instrument

**Please Note: this project is still in early stage**

## Intro

Connect your android device and text your app's name com.compnay.app, then press launch and you're done. Normally the stack trace will contain function address infomations, you will need to provide addr2line's path by clicking Addr2line button and select the correct executable in your ndk path. Then click Load Symblos to select the correct symblo for target .so library, then those address will be translated to human readable format.

![](images/screenshot.gif)

## Unity Integration

First copy plugins/Unity/LoliProfiler to your unity project, then build the native hooking plugin plugins/Android using correct NDK (r10e for example), and copy the built so to LoliProfiler/Android folder, at last add LoliProfiler to any long living GameObject and that's all.

## Plan

**Future plans**

* hook more memory functions realloc/free/etc ...
* and more ... 

## Building

**Requirments**

* QT 5 or greater
* QtCharts plugin installed
* QT Creater 4.8 or greater
* C++11 Compiler

## Links

* xHook https://github.com/iqiyi/xHook
* App Icon https://www.flaticon.com/authors/smashicons
* Prebuild Binaries https://git.code.oa.com/xinhou/loli_profiler/wikis/home
