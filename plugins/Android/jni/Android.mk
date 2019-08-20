LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE     := libloli
LOCAL_C_INCLUDES := $(LOCAL_PATH)
LOCAL_CFLAGS     := -Wall -Wextra -Werror -fvisibility=hidden -std=c11
LOCAL_CXXFLAGS   := -Wall -Wextra -Werror -fvisibility=hidden -std=c++11
LOCAL_SRC_FILES  := loli.cpp \
                    lz4/lz4.c \
                    xhook.c \
                    xh_core.c \
                    xh_elf.c \
                    xh_jni.c \
                    xh_log.c \
                    xh_util.c \
                    xh_version.c
LOCAL_LDLIBS     := -llog -latomic

include $(BUILD_SHARED_LIBRARY)
