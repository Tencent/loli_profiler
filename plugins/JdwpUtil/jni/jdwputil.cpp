#ifndef JDWPUTIL_CPP
#define JDWPUTIL_CPP

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <cassert>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <android/log.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <inttypes.h>
#include <jni.h>
#include <regex.h>

#include "loli_dlfcn.h"

int RestartJDWP();

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    __android_log_print(ANDROID_LOG_INFO, "Loli", "JNI_OnLoad");
    RestartJDWP();
    return JNI_VERSION_1_6;
}

enum JdwpTransportType {
    kJdwpTransportNone = 0,
    kJdwpTransportUnknown,      // Unknown tranpsort
    kJdwpTransportSocket,       // transport=dt_socket
    kJdwpTransportAndroidAdb,   // transport=dt_android_adb
};

// gnustl_static
struct JdwpOptions {
    JdwpTransportType transport = kJdwpTransportAndroidAdb;
    bool server = true;
    bool suspend = false;
    std::string host = "";
    uint16_t port = static_cast<uint16_t>(8700);
};

int RestartJDWP() {
    // /apex/com.android.runtime/lib64/
    void *handler = fake_dlopen("/apex/com.android.runtime/lib/libart.so", RTLD_NOW);
    // void *handler = fake_dlopen("/system/lib/libart.so", RTLD_NOW);
    if (handler == NULL) {
        __android_log_print(ANDROID_LOG_INFO, "Loli", "Error fake_dlopen");
        return -1;
    }

    void (*allowJdwp)(bool);
    allowJdwp = (void (*)(bool)) fake_dlsym(handler, "_ZN3art3Dbg14SetJdwpAllowedEb");
    if (allowJdwp == NULL) {
        __android_log_print(ANDROID_LOG_INFO, "Loli", "Error fake_dlsym");
        return -1;
    }
    allowJdwp(true);
    
    void (*pfun)();
    pfun = (void (*)()) fake_dlsym(handler, "_ZN3art3Dbg8StopJdwpEv");
    if (pfun == NULL) {
        __android_log_print(ANDROID_LOG_INFO, "Loli", "Error fake_dlsym");
        return -1;
    }
    pfun();

    // bool (*parseJdwpOptions)(const std::string&, void*);
    // parseJdwpOptions = (bool (*)(const std::string&, void*)) fake_dlsym(handler,
    //     "_ZN3art4JDWP16ParseJdwpOptionsERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEEPNS0_11JdwpOptionsE"); // Android 9
    //     // "_ZN3art3Dbg16ParseJdwpOptionsERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE"); // Early version
    // // std::string options = "transport=dt_android_adb,server=y,suspend=y";
    // std::string options = "transport=dt_socket,address=8700,server=y,suspend=n";

    // if (parseJdwpOptions == NULL) {
    //     __android_log_print(ANDROID_LOG_INFO, "Loli", "Error fake_dlsym");
    //     return -1;
    // }
    JdwpOptions jdwpOptions;
    // parseJdwpOptions(options, &jdwpOptions);

    // _ZN3art3Dbg13ConfigureJdwpERKNS_4JDWP11JdwpOptionsE
    void (*configureJdwp)(const JdwpOptions& jdwp_opts);
    configureJdwp = (void (*)(const JdwpOptions& jdwp_opts)) fake_dlsym(handler, 
        "_ZN3art3Dbg13ConfigureJdwpERKNS_4JDWP11JdwpOptionsE");
    if (configureJdwp) {
        configureJdwp(jdwpOptions);
    }

    bool (*isJdwpConfigured)();
    isJdwpConfigured = (bool (*)()) fake_dlsym(handler, 
        "_ZN3art3Dbg16IsJdwpConfiguredEv");
    if (isJdwpConfigured != NULL) {
        __android_log_print(ANDROID_LOG_INFO, "Loli", "isJdwpConfigured: %s", isJdwpConfigured() ? "true" : "false");
    } else {
        __android_log_print(ANDROID_LOG_INFO, "Loli", "Error _ZN3art3Dbg16IsJdwpConfiguredEv");
    }

    pfun = (void (*)()) fake_dlsym(handler, "_ZN3art3Dbg9StartJdwpEv");
    if (pfun == NULL) {
        __android_log_print(ANDROID_LOG_INFO, "Loli", "Error fake_dlsym");
        return -1;
    }
    pfun();
    return 0;
}

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // JDWPUTIL_CPP