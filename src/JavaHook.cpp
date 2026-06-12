#include "JavaHook.h"
#include <vector>
#include <Windows.h>
#include "Module.h"
#include <iostream>

// ---- Internal state ----
static JNIEnv* g_env = nullptr;

static void* find_correct_hook_place(void* _i2i_entry);
static void common_detour(HotSpot::frame* frame, HotSpot::Thread* thread, bool* cancel);

static constexpr int NO_COMPILE =
    HotSpot::JVM_ACC_NOT_C2_COMPILABLE |
    HotSpot::JVM_ACC_NOT_C1_COMPILABLE |
    HotSpot::JVM_ACC_NOT_C2_OSR_COMPILABLE |
    HotSpot::JVM_ACC_QUEUED;

// ---- Internal data structures ----

struct i2iHookData
{
    void* _i2i_entry = nullptr;
    class Midi2iHook* hook = nullptr;
};
static std::vector<i2iHookData> hooked_i2i_entries{};

struct HookedMethod
{
    HotSpot::Method* method = nullptr;
    JavaHook::DetourCallback detour = nullptr;
};
static std::vector<HookedMethod> hooked_methods{};

// ---- Midi2iHook (internal trampoline class) ----

class Midi2iHook
{
public:
    Midi2iHook(uint8_t* target, JavaHook::DetourCallback detour);
    ~Midi2iHook();

    bool is_error = true;
private:
    uint8_t* target = nullptr;
    uint8_t* allocated_assembly = nullptr;
};

// ---- Public API ----

bool JavaHook::init()
{
    return HotSpot::init();
}

bool JavaHook::hook(jvmtiEnv* tienv, JNIEnv* env, jmethodID methodID, DetourCallback detour)
{
    // One-time JVMTI capability setup
    static bool capabilities_added = false;
    if (!capabilities_added && tienv)
    {
        jvmtiCapabilities capabilities{};
        capabilities.can_retransform_classes = JVMTI_ENABLE;
        tienv->AddCapabilities(&capabilities);
        capabilities_added = true;
    }

    if (!methodID || !detour || !tienv || !env)
        return false;

    // Store env for use by common_detour
    g_env = env;

    HotSpot::Method* method = *(HotSpot::Method**)methodID;

    // Avoid double-hooking
    for (HookedMethod& hk : hooked_methods)
    {
        if (hk.method == method)
            return true;
    }

    // Prevent JIT compilation and inlining
    method->set_dont_inline(true);
    int* flags = (int*)method->get_access_flags();
    *flags |= (NO_COMPILE);

    // Retransform trick: discard any already-compiled code
    jclass owner = nullptr;
    tienv->GetMethodDeclaringClass(methodID, &owner);
    tienv->RetransformClasses(1, &owner);
    env->DeleteLocalRef(owner);

    // Re-fetch method (JVMTI may have moved it)
    method = *(HotSpot::Method**)methodID;
    method->set_dont_inline(true);
    flags = (int*)method->get_access_flags();
    *flags |= (NO_COMPILE);

    // Register the detour callback
    hooked_methods.push_back({ method, detour });

    // Check if we already hooked this i2i entry
    bool hook_new_i2i = true;
    void* i2i = method->get_i2i_entry();
    for (i2iHookData& hk : hooked_i2i_entries)
    {
        if (hk._i2i_entry == i2i)
            hook_new_i2i = false;
    }

    if (!hook_new_i2i) return true;

    // Find the correct hook location in the i2i stub
    uint8_t* target = (uint8_t*)find_correct_hook_place(i2i);
    if (!target)
    {
        std::cerr << "Failed to find correct i2i hook location\n";
        std::cout << "Debug: i2ientry: " << i2i << '\n';
        return false;
    }
    std::cout << "placed i2i hook at: " << (void*)target << '\n';

    Midi2iHook* hook = new Midi2iHook(target, common_detour);
    if (!hook)
        return false;

    hooked_i2i_entries.push_back({ i2i, hook });
    return true;
}

void JavaHook::clean()
{
    for (i2iHookData& hk : hooked_i2i_entries)
    {
        delete hk.hook;
    }
    hooked_i2i_entries.clear();

    for (HookedMethod& hm : hooked_methods)
    {
        hm.method->set_dont_inline(false);
        int* flags = (int*)hm.method->get_access_flags();
        *flags &= ~(NO_COMPILE);
    }
    hooked_methods.clear();

    g_env = nullptr;
}

// ---- Internal helpers ----

static void* find_correct_hook_place(void* _i2i_entry)
{
    uint8_t pattern[] =
    {
        0x89, 0x84, 0x24, 0x90, 0x90, 0x90, 0x90,
        0x89, 0x84, 0x24, 0x90, 0x90, 0x90, 0x90,
        0x89, 0x84, 0x24, 0x90, 0x90, 0x90, 0x90,
        0x89, 0x84, 0x24, 0x90, 0x90, 0x90, 0x90,
        0x41, 0xC6, 0x87, 0x90, 0x90, 0x90, 0x90, 0x00
    };

    uint8_t pattern2[] =
    {
        0x4C, 0x8B, 0x75, 0x90, 0xC3
    };

    uint8_t* curr = (uint8_t*)_i2i_entry;
    while (curr < (uint8_t*)_i2i_entry + 0x400)
    {
        int matches = 0;
        for (int i = 0; i < sizeof(pattern); ++i)
        {
            if (pattern[i] == 0x90 || pattern[i] == curr[i])
                matches++;
            else
                break;
        }
        if (matches == sizeof(pattern))
        {
            for (uint8_t* curr2 = curr; curr2 > curr - 100; --curr2)
            {
                int matches2 = 0;
                for (int j = 0; j < sizeof(pattern2); ++j)
                {
                    if (pattern2[j] == 0x90 || pattern2[j] == curr2[j])
                        matches2++;
                    else
                        break;
                }
                if (matches2 == sizeof(pattern2))
                    HotSpot::frame::locals_offset = *(char*)(curr2 + 3);
            }
            return curr + sizeof(pattern) - 8;
        }
        curr += 1;
    }

    // 8 = sizeof: mov BYTE PTR [r15+_do_not_unlock_if_synchronized_offset], 0x0
    // We place the hook here because there's enough space and it's at the right time
    return nullptr;
}

static void common_detour(HotSpot::frame* frame, HotSpot::Thread* thread, bool* cancel)
{
    if (!(*(void**)thread->get_env()) ||
        thread->get_thread_state() != HotSpot::_thread_in_Java ||
        !g_env)
        return;

    for (HookedMethod& hk : hooked_methods)
    {
        if (hk.method == frame->get_method())
        {
            hk.detour(frame, thread, cancel);
            thread->set_thread_state(HotSpot::_thread_in_Java);
            return;
        }
    }
}

// ---- Midi2iHook implementation ----

Midi2iHook::Midi2iHook(uint8_t* _target, JavaHook::DetourCallback detour) :
    target(_target)
{
    constexpr int HOOK_SIZE = 8;
    constexpr int JMP_SIZE = 5;
    constexpr int JE_OFFSET = 0x3d;
    constexpr int JE_SIZE = 6;
    constexpr int DETOUR_ADDRESS_OFFSET = 0x56;

    /*
    push   rax
    push   rcx
    push   rdx
    push   r8
    push   r9
    push   r10
    push   r11
    push   rbp
    push   0x0
    mov    rcx,rbp
    mov    rdx,r15
    lea    r8,[rsp]
    mov    rbp,rsp
    and    rsp,0xfffffffffffffff0
    sub    rsp,0x20
    call   [rip+data]
    mov    rsp,rbp
    pop    rax
    cmp    rax,0x0
    pop    rbp
    pop    r11
    pop    r10
    pop    r9
    pop    r8
    pop    rdx
    pop    rcx
    pop    rax
    je     0x11111111
    movq   xmm0,rax
    mov    rbx,QWORD PTR [rbp-0x8]
    mov    rsp,rbp
    pop    rbp
    pop    rsi
    mov    rsp,rbx
    jmp    rsi
    data:
    */

    uint8_t assembly[] =
    {
        0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, 0x55,
        0x6A, 0x00, 0x48, 0x89, 0xE9, 0x4C, 0x89, 0xFA, 0x4C, 0x8D, 0x04, 0x24,
        0x48, 0x89, 0xE5, 0x48, 0x83, 0xE4, 0xF0, 0x48, 0x83, 0xEC, 0x20,
        0xFF, 0x15, 0x2D, 0x00, 0x00, 0x00,
        0x48, 0x89, 0xEC, 0x58, 0x48, 0x83, 0xF8, 0x00, 0x5D,
        0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58,
        0x0F, 0x84, 0x00, 0x00, 0x00, 0x00,
        0x66, 0x48, 0x0F, 0x6E, 0xC0,
        0x48, 0x8B, 0x5D, 0xF8, 0x48, 0x89, 0xEC, 0x5D, 0x5E,
        0x48, 0x89, 0xDC, 0xFF, 0xE6,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 // detour address placeholder
    };

    allocated_assembly = JavaHook::detail::Module::allocate_nearby_memory(
        target, HOOK_SIZE + sizeof(assembly), PAGE_EXECUTE_READWRITE);

    if (!allocated_assembly)
    {
        std::cerr << "Failed to allocate memory for i2i hook\n";
        return;
    }

    int32_t jmp_back_delta = (int32_t)(target + HOOK_SIZE -
        (allocated_assembly + HOOK_SIZE + JE_OFFSET + JE_SIZE));
    *(int32_t*)(assembly + JE_OFFSET + 2) = jmp_back_delta;

    // Store detour function pointer at the data section
    JavaHook::DetourCallback* detour_slot =
        (JavaHook::DetourCallback*)(assembly + DETOUR_ADDRESS_OFFSET);
    *detour_slot = detour;

    memcpy(allocated_assembly, target, HOOK_SIZE);
    memcpy(allocated_assembly + HOOK_SIZE, assembly, sizeof(assembly));

    DWORD original_prot = 0;
    VirtualProtect(allocated_assembly, HOOK_SIZE + sizeof(assembly),
        PAGE_EXECUTE_READ, &original_prot);

    // Place the JMP at the target
    VirtualProtect(target, JMP_SIZE, PAGE_EXECUTE_READWRITE, &original_prot);
    target[0] = 0xE9U;
    int32_t jmp_detour_delta = (int32_t)(allocated_assembly - (target + JMP_SIZE));
    *(int32_t*)(target + 1) = jmp_detour_delta;
    VirtualProtect(target, JMP_SIZE, original_prot, &original_prot);

    is_error = false;
}

Midi2iHook::~Midi2iHook()
{
    if (is_error)
        return;

    DWORD original = 0;
    if (target[0] == 0xE9U &&
        VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &original))
    {
        memcpy(target, allocated_assembly, 5);
        VirtualProtect(target, 5, original, &original);
    }

    VirtualFree(allocated_assembly, 0, MEM_RELEASE);
}
