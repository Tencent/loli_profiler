LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE     := libjdwputil
LOCAL_C_INCLUDES := $(LOCAL_PATH)
LOCAL_CFLAGS     := -Wall -Wextra -Werror -fvisibility=hidden -std=c11
LOCAL_CXXFLAGS   := -Wall -Wextra -Werror -fvisibility=hidden -std=c++11
LOCAL_SRC_FILES  := jdwputil.cpp \
					loli_dlfcn.c
LOCAL_LDLIBS     := -llog -latomic

include $(BUILD_SHARED_LIBRARY)
