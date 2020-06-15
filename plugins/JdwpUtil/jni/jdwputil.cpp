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
#include <sys/system_properties.h>
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

#define LOLILOGI(...) __android_log_print(ANDROID_LOG_INFO, "Loli", __VA_ARGS__)
#define LOLILOGW(...) __android_log_print(ANDROID_LOG_WARN, "Loli", __VA_ARGS__)
#define LOLILOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Loli", __VA_ARGS__)

int RestartJDWP();

// https://developer.android.com/guide/topics/manifest/uses-sdk-element#ApiLevels
// API Level  Platform Version
// 29         Android 10.0
// 28         Android 9
// 21         Android 5.0
int GetAPILevel() {
    char osVersion[PROP_VALUE_MAX+1];
    int osVersionLength = __system_property_get("ro.build.version.sdk", osVersion);
    if (osVersionLength >= 0) {
        osVersion[osVersionLength] = '\0';
        return (int)atoi(osVersion);
    }
    return -1;
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    LOLILOGI("JNI_OnLoad");
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
    int apiLevel = GetAPILevel();
    if (apiLevel > 29 || apiLevel < 21) {
        LOLILOGE("Unsupported Android Version %i", apiLevel);
        return -1;
    }

    void *handler = nullptr;
    if (apiLevel == 29) { // Android 10
        handler = fake_dlopen("/apex/com.android.runtime/lib/libart.so", RTLD_NOW);
    } else {
        handler = fake_dlopen("/system/lib/libart.so", RTLD_NOW);
    }
    if (handler == NULL) {
        LOLILOGE("Error fake_dlopen libart.so");
        return -1;
    }

    void (*allowJdwp)(bool);
    allowJdwp = (void (*)(bool)) fake_dlsym(handler, "_ZN3art3Dbg14SetJdwpAllowedEb");
    if (allowJdwp == NULL) {
        LOLILOGE("Error fake_dlsym _ZN3art3Dbg14SetJdwpAllowedEb");
        return -1;
    }
    allowJdwp(true);
    
    void (*stopJdwp)() = (void (*)()) fake_dlsym(handler, "_ZN3art3Dbg8StopJdwpEv");
    if (stopJdwp == NULL) {
        LOLILOGE("Error fake_dlsym _ZN3art3Dbg8StopJdwpEv");
        return -1;
    }
    stopJdwp();

    if (apiLevel == 29) { // Android 10
        JdwpOptions jdwpOptions;
        void (*configureJdwp)(const JdwpOptions& jdwp_opts);
        configureJdwp = (void (*)(const JdwpOptions& jdwp_opts)) fake_dlsym(handler, 
            "_ZN3art3Dbg13ConfigureJdwpERKNS_4JDWP11JdwpOptionsE");
        if (configureJdwp == nullptr) {
            LOLILOGE("Error fake_dlsym _ZN3art3Dbg13ConfigureJdwpERKNS_4JDWP11JdwpOptionsE");
            return -1;
        }
        configureJdwp(jdwpOptions);
    } else {
        auto mangledName = apiLevel == 28 ? 
            "_ZN3art4JDWP16ParseJdwpOptionsERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEEPNS0_11JdwpOptionsE" :
            "_ZN3art3Dbg16ParseJdwpOptionsERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE";

        bool (*parseJdwpOptions)(const std::string&, void*);
        parseJdwpOptions = (bool (*)(const std::string&, void*)) fake_dlsym(handler, mangledName);
        std::string options = "transport=dt_android_adb,address=8700,server=y,suspend=n";

        if (parseJdwpOptions == NULL) {
            LOLILOGE("Error fake_dlsym parseJdwpOptions");
            return -1;
        }
        JdwpOptions jdwpOptions;
        parseJdwpOptions(options, &jdwpOptions);
    }

    void (*startJdwp)() = (void (*)()) fake_dlsym(handler, "_ZN3art3Dbg9StartJdwpEv");
    if (startJdwp == NULL) {
        LOLILOGE("Error fake_dlsym _ZN3art3Dbg9StartJdwpEv");
        return -1;
    }
    startJdwp();
    return 0;
}

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // JDWPUTIL_CPP