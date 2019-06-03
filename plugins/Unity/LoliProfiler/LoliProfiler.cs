using UnityEngine;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;

public class LoliProfiler : MonoBehaviour {

#if UNITY_ANDROID && !UNITY_EDITOR
    [DllImport("loli")]
    private static extern int loliHook();
    [DllImport("loli")]
    private static extern void loliTick();
#else
    private static int loliHook() { return 0; }
    private static void loliTick() {}
#endif

    private string loliFilePath;

    private void Awake()
    {
        Debug.Log("loliHookStatus: " + loliHook());
    }
    
    private void Update()
    {
        loliTick();
    }
}
