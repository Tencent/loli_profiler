# Trouble Shooting

## Before Starting

You should **close Android Studio / Unreal Engine Editor** or any other process that **uses adb bridge**.

You may need to call **sudo spctl --master-disable** on recent version of Mac OS before use.

Please make sure your apk's [debuggable](https://stackoverflow.com/questions/2952140/android-how-to-mark-my-app-as-debuggable) flag is set to true. Unless your device is rooted.

Make sure your apk has Internet Access, profiler need this to send data to your PC.

## Quick Start

![](images/captured.png)

Profiler will print simple logs in DashBoard tab's console window. 

And will print some more logs under installation folder.

Use **adb logcat -s Loli** to see application runtime logs.

Use **adb logcat -s xhook** to see if your target library is hooked correctly.

Always save these logs when you're in trouble. It helps us to identify the key problem.