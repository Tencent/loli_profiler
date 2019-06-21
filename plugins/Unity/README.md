```bash
# 当前目录plugins/Unity
cd Android
# 需要android-ndk-r10e或更高
android-ndk-r13b/ndk-build
cd ../
# 编译完再LoliProfiler下新建文件夹
mkdir LoliProfiler/Plugins/Android/
# 回到顶层目录plugins/Unity
cd ../../../
# 拷贝so到LoliProfiler/Plugins/Android/下
cp Android/libs/armeabi-v7a/libloli.so LoliProfiler/Plugins/Android/libloli.so
cp Android/libs/armeabi-v7a/libxhook.so LoliProfiler/Plugins/Android/libxhook.so
# 最后将LoliProfiler文件夹拷贝到Unity工程如Assets/Plugins目录下即可
# 整合完毕后将LoliProfiler.cs脚本挂在一个常驻游戏的GameObject即完成插件整合
```

