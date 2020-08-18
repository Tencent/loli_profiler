APP_OPTIM        := release
APP_ABI          := armeabi-v7a arm64-v8a
ifdef LLVM
APP_STL          := c++_static
else
APP_STL          := gnustl_static
endif
APP_PLATFORM     := android-16