using UnityEngine;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;

public class LoliProfiler : MonoBehaviour {

#if UNITY_ANDROID && !UNITY_EDITOR
    [DllImport("loli", CallingConvention = CallingConvention.Cdecl)]
    private static extern int loliHook(string soNames);
#else
    private static int loliHook(string soNames) { return 0; }
#endif

    private void Awake()
    {
        var ecode = loliHook("libil2cpp,libunity");
        if (ecode != 0)
            Debug.Log("loliHookError: " + ecode);
    }
}
