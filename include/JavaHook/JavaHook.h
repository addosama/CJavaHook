#pragma once
#include <JNI/jni.h>
#include <JNI/jvmti.h>
#include "HotSpot.h"

namespace JavaHook
{
    /// <summary>
    /// Callback type for method detours.
    /// Parameters:
    ///   frame  - current HotSpot interpreter frame (use get_param helpers to extract arguments)
    ///   thread - current Java thread
    ///   cancel - set to true to prevent the original method from executing;
    ///            use set_return_value() to provide a replacement return value.
    /// </summary>
    typedef void(*DetourCallback)(HotSpot::frame* frame, HotSpot::Thread* thread, bool* cancel);

    /// <summary>
    /// Initialize the HotSpot introspection layer.
    /// Must be called once before any other API functions, and only when
    /// the hosting process is a JVM with exported VMStructs symbols.
    /// Returns true on success.
    /// </summary>
    bool init();

    /// <summary>
    /// Hook a Java method by its jmethodID.
    ///
    /// This prevents JIT compilation of the method, retransforms the owning
    /// class to discard any already-compiled code, and places an inline hook
    /// on the interpreter-to-interpreter (i2i) entry point.
    ///
    /// Parameters:
    ///   tienv   - JVMTI environment (used for AddCapabilities / RetransformClasses)
    ///   env     - JNI environment (used for local-reference cleanup)
    ///   method  - jmethodID of the target method
    ///   detour  - callback invoked every time the method is entered
    ///
    /// Returns true on success, false if the hook could not be placed.
    /// </summary>
    bool hook(jvmtiEnv* tienv, JNIEnv* env, jmethodID methodID, DetourCallback detour);

    /// <summary>
    /// Remove all active hooks and restore original method properties
    /// (re-enable inlining and JIT compilation).
    /// Call this before unloading the library.
    /// </summary>
    void clean();

    // ---- Parameter extraction helpers (call from within DetourCallback) ----

    /// Extract a primitive-type parameter from the interpreter frame.
    /// index 0 = 'this' (for instance methods), index 1 = first declared parameter, etc.
    template <typename T>
    inline T get_primitive_param(HotSpot::frame* frame, int index)
    {
        return *(T*)(frame->get_locals() - index);
    }

    /// Extract a jobject parameter from the interpreter frame.
    inline jobject get_jobject_param(HotSpot::frame* frame, int index)
    {
        return (jobject)(frame->get_locals() - index);
    }

    /// Set the return value when the method is cancelled.
    /// The memory layout relies on the trampoline assembly; see JavaHook.cpp for details.
    template <typename T>
    inline void set_return_value(bool* cancel, T value)
    {
        *(T*)((void**)cancel + 8) = value;
    }
}
