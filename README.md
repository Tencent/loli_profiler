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
* 制作Unreal插件
* 计划中 ... 

## 编译

**环境**

* QT 5 或更高
* 安装QtCharts插件
* QT Creater 4.8 或更高
* C++11 编译器

## 常见问题

* 点击Launch提示：Error starting app by adb monkey
  首先确认你可以在命令行访问adb命令，如果可以，则表明此安卓sdk版本过低不支持monkey，请升级安卓sdk，如果您本地有多个sdk版本，也可以在程序里Select SDK选择到更新版本的sdk的跟目录即可
* 截图截出的图片一直是竖屏
  我使用adb exec-out screencap -p命令截图，我发现在一些机器上截出来的图就是横竖不分的 。。暂时没有更好的办法，如果截图重要，建议换台手机或者更新手机系统来测试
* 我发现时不时app取到的数据的时间不对
  确认C#层没有多次调用loliHook，调用loliHook会重置时间戳，只建议在游戏开始时调用一次
* 点击Launch成功后App正常启动了，但是一直在提示Connection Lost
  LoliProfiler会在拉起程序后尝试连接apk中的服务器，连接失败会每秒重连一次，重连尝试10次后会判断apk没有开启服务器，并停止。因此确定App中调用loliHook的时机是在App开始的时候，loliHook会拉起App中的服务器
* 抓取的数据好像不是很全？
  我们默认抓取libil2cpp.so中的malloc、calloc、free，也就是会默认抓取unity程序中由gameplay相关代码发起的所有内存io函数堆栈。如果您想抓取如libunity.so的内存io数据，可参考loli.cpp中的loliHook函数进行扩展并重新编译安卓的loli.so（我计划调整loliHook的API，在拉起时去传入想要注入的so与内存函数，就不需要重新编译loli.so了）
* Unity程序抓取的数据都是包含mono字段的调用，看不到C#的堆栈
  建议使用Unity的il2cpp编译模式，我们hook的是C语言的内存io函数，因此无法获取mono运行时的信息

## 链接

* KM介绍文章 http://km.oa.com/articles/show/408991
* xHook https://github.com/iqiyi/xHook
* 图标 https://www.flaticon.com/authors/smashicons
* 定期预编译的程序（包含UnityNative插件） https://git.code.oa.com/xinhou/loli_profiler/wikis/home
