LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := xhook-prebuilt
LOCAL_SRC_FILES := xhook/lib/$(TARGET_ARCH_ABI)/libxhook.so
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/xhook/include

include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)

# override strip command to strip all symbols from output library; no need to ship with those..
# cmd-strip = $(TOOLCHAIN_PREFIX)strip $1 

LOCAL_MODULE    := libloli
LOCAL_CFLAGS    := -Werror
LOCAL_CXXFLAGS  := -Werror -std=c++11
LOCAL_SRC_FILES := loli.cpp
LOCAL_LDLIBS    := -llog
LOCAL_SHARED_LIBRARIES := xhook-prebuilt

include $(BUILD_SHARED_LIBRARY)
