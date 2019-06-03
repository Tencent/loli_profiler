# Loli Profiler

轻量开源内存分析工具（Lightweight Opensource profiLing Instrument）

**注意：此程序仍处于初级研发阶段**

## 简介

首先整合对应（Unity、Unreal）插件至游戏apk中，打开LoliProfiler，输入apk程序名称如：com.company.name，点击Launch即可。一般情况下堆栈数据会包含函数地址信息，你需要提供安卓NDK工具链中的addr2line可执行程序的路径给LoliProfiler。接着就可以选择Load Symbol来加载符号表数据，当翻译完成后，StackTrace中的数据就会被翻译为真正的函数名称。

![](images/screenshot.gif)

## 特性

* 可hook目标APK中任意so库
* 可将函数地址自动批量转换为函数名称
* 可过滤常驻内存
* 每5s自动截图一次
* 基于时间线的过滤交互操作
* 从手机端实时获取内存相关函数的堆栈信息（通过TCP Socket）
* 运行流畅（使用C++与QT开发）

## Unity整合

首先将plugins/Unity/LoliProfiler拷贝到你的Unity工程下，接着需要用NDK（如r10e）来编译 plugins/Android下的Unity安卓native插件，并将编译出的插件放到LoliProfiler/Android目录下。最后将LoliProfiler.cs中的Component挂载到一个常驻游戏的GameObject上即可。

## Unreal整合

WIP

## 计划

**短期计划**

* hook 更多的内存相关函数 realloc/etc ... 
* 计划中 ... 

## 编译

**环境**

* QT 5 或更高
* 安装QtCharts插件
* QT Creater 4.8 或更高
* C++11 编译器

## 链接

* xHook https://github.com/iqiyi/xHook
* 图标 https://www.flaticon.com/authors/smashicons
* 定期预编译的程序（包含UnityNative插件） https://git.code.oa.com/xinhou/loli_profiler/wikis/home
