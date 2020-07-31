LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE     := libloli
LOCAL_C_INCLUDES := $(LOCAL_PATH)
LOCAL_CFLAGS     := -Wall -Wextra -Werror -std=c11 -O2
LOCAL_CXXFLAGS   := -Wall -Wextra -Werror -std=c++11 -O2
LOCAL_SRC_FILES  := loli.cpp \
                    loli_server.cpp \
                    loli_utils.cpp \
                    loli_dlfcn.c \
                    lz4/lz4.c \
                    xhook.c \
                    xh_core.c \
                    xh_elf.c \
                    xh_jni.c \
                    xh_log.c \
                    xh_util.c \
                    xh_version.c\
                    wrapper/wrapper.cpp
ifdef LLVM
LOCAL_LDLIBS     := -llog
else
LOCAL_LDLIBS     := -llog -latomic
endif

include $(BUILD_SHARED_LIBRARY)
