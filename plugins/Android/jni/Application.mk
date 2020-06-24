APP_OPTIM        := release
ifdef LLVM
APP_ABI          := armeabi-v7a arm64-v8a
APP_STL          := c++_static
else
APP_ABI          := armeabi armeabi-v7a arm64-v8a
APP_STL          := gnustl_static
endif
APP_PLATFORM     := android-16