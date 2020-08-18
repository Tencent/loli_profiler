This is the plugin source directory for UnrealEngine4.

You need to copy the correct libjdwputil.so libraries to ThirdParty/Android/ folder：

1. If Apk is packed with NDK r16b or lower.
   You're using gcc, you should copy folder gcc's content to ThirdParty/Android/.
2. If Apk is packed with NDK higher than r16b.
   You're using clang, you should copy folder llvm's content to ThirdParty/Android/.

After these steps, your ThirdParty/Android should look like this:

```
ThirdParty/Android
  armeabi-v7a/libjdwputil.so
  arm64-v8a/libjdwputil.so
```