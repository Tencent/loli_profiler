LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE     := libjdwputil
LOCAL_C_INCLUDES := $(LOCAL_PATH)
LOCAL_CFLAGS     := -Wall -Wextra -Werror -fvisibility=default -std=c11
LOCAL_CXXFLAGS   := -Wall -Wextra -Werror -fvisibility=default -std=c++11
LOCAL_SRC_FILES  := jdwputil.cpp \
					loli_dlfcn.c
ifdef LLVM
LOCAL_LDLIBS     := -llog
else
LOCAL_LDLIBS     := -llog -latomic
endif

include $(BUILD_SHARED_LIBRARY)