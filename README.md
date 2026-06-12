# JavaHookLib — 简明文档
> from https://github.com/Yumiera/RiptermsGhostQM    
    
> 一个独立的 C++20 静态库，用于在 HotSpot JVM 进程内**通过 i2i stub inline hook 拦截任意 Java 方法**。

---

## 1. 这是什么

JavaHookLib 把 [RiptermsGhost](https://github.com/Lefraudeur/RiptermsGhost) 项目中的 Java 方法 hook 子系统抽取为独立的 `.lib` 文件。你只需要：

- 把你的 DLL 注入到运行 HotSpot JVM 的进程
- 拿到 `JNIEnv*` 和 `jvmtiEnv*`
- 调用 `JavaHook::hook(tienv, env, methodID, callback)`

不需要 `-javaagent`，不需要理解字节码，不需要外部 jar。

---

## 2. 文件结构

```
JavaHookLib/
├── include/
│   └── JavaHook/
│       ├── JavaHook.h       ★ 唯一需要 #include 的头文件
│       ├── HotSpot.h          HotSpot JVM 内部类型（回调参数用到）
│       ├── Module.h           内部工具：内存分配 / 模式扫描
│       └── Capstone.h         内部工具：反汇编（找 hook 长度）
├── src/
│   ├── JavaHook.cpp          核心：i2i trampoline + hook 管理
│   ├── HotSpot.cpp           VMStructs 自省层
│   ├── Module.cpp            内存 / PE 工具实现
│   └── Capstone.cpp          Capstone 封装
└── CMakeLists.txt
```

---

## 3. 外部依赖（使用者提供）

| 依赖 | 用途 | 来源 |
|------|------|------|
| JNI 头文件 (`jni.h`, `jvmti.h`) + `jvm.lib` | JNI / JVMTI 类型 | JDK 安装目录 |
| Capstone (`capstone.h` + `capstone.lib`) | 反汇编（计算 inline hook 覆盖长度） | [capstone-engine.org](https://www.capstone-engine.org) |
| Windows SDK (`Windows.h`, `Psapi.h`) | 内存管理、PE 解析 | Visual Studio / Windows SDK |
| HotSpot JVM（运行时） | `gHotSpotVMStructs` 等导出符号 | 进程必须是 HotSpot JVM |

> Capstone 的 `capstone.lib` 和 JNI 的 `jvm.lib` 也在原项目 `Lib/` 目录下有现成的。

---

## 4. 构建

```bash
cmake -B build -S JavaHookLib \
    -DCAPSTONE_INCLUDE_DIR=C:/path/to/capstone/include \
    -DCAPSTONE_LIBRARY=C:/path/to/capstone.lib \
    -DJNI_INCLUDE_DIRS="C:/path/to/jdk/include;C:/path/to/jdk/include/win32"

cmake --build build --config Release
# 产物：build/Release/JavaHookLib.lib
```

或者直接在 Visual Studio 里把 `include/JavaHook/` 加入 include 路径，`src/*.cpp` 加入项目即可。

---

## 5. API 参考

### 5.1 `JavaHook::init()`

```cpp
bool init();
```

验证当前 JVM 是否导出了必需的 VMStructs 符号表。必须在 `hook()` 之前调用一次。

| 返回值 | 含义 |
|--------|------|
| `true` | 正常，可以继续 |
| `false` | 当前进程不是 HotSpot JVM，或 VMStructs 导出未开启 |

---

### 5.2 `JavaHook::hook()`

```cpp
bool hook(jvmtiEnv* tienv,
          JNIEnv*   env,
          jmethodID methodID,
          DetourCallback detour);
```

拦截一个 Java 方法的**解释器入口**。每当该方法被 HotSpot 解释执行时（且线程状态为 `_thread_in_Java`），`detour` 会被调用。

**参数**：

| 参数 | 类型 | 说明 |
|------|------|------|
| `tienv` | `jvmtiEnv*` | 用于 `AddCapabilities` + `RetransformClasses` 清除已编译代码 |
| `env` | `JNIEnv*` | 用于 `DeleteLocalRef` 清理局部引用 |
| `methodID` | `jmethodID` | 标准 JNI 方法 ID |
| `detour` | `DetourCallback` | 回调函数（见 5.4） |

**返回值**：`true` 成功，`false` 失败（i2i entry 找不到、内存分配失败等）。

**副作用**：目标方法会被标记为 `dont_inline` 且禁止 C1/C2 JIT 编译。

---

### 5.3 `JavaHook::clean()`

```cpp
void clean();
```

恢复所有被 `hook()` 修改的方法属性（重新启用内联和 JIT），移除所有 inline hook。在 DLL 卸载前调用。

---

### 5.4 `DetourCallback` 类型

```cpp
typedef void(*DetourCallback)(
    HotSpot::frame*   frame,   // 当前解释器栈帧
    HotSpot::Thread*  thread,  // 当前 Java 线程
    bool*             cancel   // 设为 true 阻止原方法执行
);
```

---

### 5.5 参数提取辅助函数（在 DetourCallback 内使用）

```cpp
// 提取基本类型参数（float, int, double, jlong...）
// index 0 = this（实例方法）, 1 = 第一个声明参数...
template <typename T>
T get_primitive_param(HotSpot::frame* frame, int index);

// 提取 jobject 参数
jobject get_jobject_param(HotSpot::frame* frame, int index);

// 取消原方法后，设置替代返回值
// 内存布局依赖 trampoline 汇编约定
template <typename T>
void set_return_value(bool* cancel, T value);
```

---

## 6. 快速开始（完整示例）

### 场景：hook Minecraft 的 `addToSendQueue` 来拦截发包

```cpp
#include <Windows.h>
#include "JavaHook.h"

// ========== 全局状态 ==========
static jvmtiEnv* g_tienv = nullptr;

// ========== 目标 detour ==========
void onAddToSendQueue(HotSpot::frame* frame, HotSpot::Thread* thread, bool* cancel)
{
    JNIEnv* env = thread->get_env();

    // 提取参数（对应 Java 签名: void addToSendQueue(Packet p)）
    jobject netHandler = JavaHook::get_jobject_param(frame, 0);  // this
    jobject packet     = JavaHook::get_jobject_param(frame, 1);  // Packet

    // --- 在这里做你想做的事 ---

    // 例：取消发包
    // *cancel = true;

    // 例：修改包内容
    // jclass packetClass = env->GetObjectClass(packet);
    // jfieldID field = env->GetFieldID(packetClass, "someField", "I");
    // env->SetIntField(packet, field, newValue);
}

// ========== 初始化所有 hook ==========
void setupHooks(JNIEnv* env)
{
    jclass clazz = env->FindClass(
        "net/minecraft/client/network/NetHandlerPlayClient");
    jmethodID mid = env->GetMethodID(clazz,
        "addToSendQueue",
        "(Lnet/minecraft/network/Packet;)V");

    JavaHook::hook(g_tienv, env, mid, onAddToSendQueue);
    env->DeleteLocalRef(clazz);
}

// ========== glClear detour（拿到 JNI 入口）==========
typedef void(JNICALL* glClear_t)(JNIEnv*, jclass, jint);
glClear_t g_orig_glClear = nullptr;

void JNICALL detour_glClear(JNIEnv* env, jclass clazz, jint mask)
{
    // 只在第一次调用时做初始化
    static bool done = false;
    if (!done)
    {
        JavaVM* jvm = nullptr;
        env->GetJavaVM(&jvm);
        jvm->GetEnv((void**)&g_tienv, JVMTI_VERSION_1_2);

        JavaHook::init();
        setupHooks(env);
        done = true;
    }

    // End 键卸载
    if (GetAsyncKeyState(VK_END) & 0x8000)
    {
        JavaHook::clean();
        // 恢复 glClear hook 然后 FreeLibrary...
    }

    g_orig_glClear(env, clazz, mask);
}

// ========== DLL 入口 ==========
BOOL WINAPI DllMain(HINSTANCE dll, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        HMODULE lwjgl = GetModuleHandleA("lwjgl64.dll");
        g_orig_glClear = (glClear_t)GetProcAddress(lwjgl,
            "Java_org_lwjgl_opengl_GL11_nglClear");

        // ★ 在这里用你自己的 inline hook 实现替换 g_orig_glClear → detour_glClear
        place_your_inline_hook((void*)g_orig_glClear, (void*)detour_glClear);
    }
    return TRUE;
}
```

> 上面的 `place_your_inline_hook` 是一个 x64 inline hook（写 `JMP rel32` + trampoline），需要你自己实现。可以参考原项目中 `Ripterms/Hook/Hook.h` 的 `Ripterms::Hook<T>` 模板类——它内部使用了 `Capstone::find_bytes_to_replace` 来计算需要覆盖的字节数。

---

## 7. HotSpot::frame 参数布局

理解参数索引对于正确提取非常重要：

```
Java 方法: void foo(int a, float b, String c)

局部变量槽位（解释器栈帧）：
  [locals-0] = this     ← get_jobject_param(frame, 0)
  [locals-1] = a        ← get_primitive_param<int>(frame, 1)
  [locals-2] = b        ← get_primitive_param<float>(frame, 2)
  [locals-3] = c        ← get_jobject_param(frame, 3)

注意：long / double 占两个槽位！如果第一个参数是 double，
那么第二个参数的 index 就是 2 而不是 1。
```

---

## 8. 线程状态模型

`HotSpot::Thread` 有几个关键状态（定义在 `HotSpot.h`）：

| 状态 | 值 | 含义 |
|------|-----|------|
| `_thread_in_native` | 4 | 线程在 native 代码中 |
| `_thread_in_vm` | 6 | 线程在 VM 内部 |
| `_thread_in_Java` | 8 | 线程在执行 Java 代码 |

`common_detour` 只在 `_thread_in_Java` 状态下才分发回调，确保 hook 代码在安全的时机执行。

---

## 9. 工作原理（一句话版）

1. 设置方法的 `dont_inline` + `NO_COMPILE` 标志 → 禁止 JIT，强制走解释器
2. `RetransformClasses` → 丢弃已编译版本
3. 在 `_i2i_entry`（解释器到解释器入口）的 stub 代码末尾放置 `JMP` → 跳转到手写汇编 trampoline
4. Trampoline 保存寄存器 → 调用 `common_detour` 匹配并分发回调 → 根据 `cancel` 标志决定是否执行原方法
5. 回调中通过 `frame->get_locals()` 读取参数，通过 `thread->get_env()` 安全使用 JNI

---

## 10. 限制与注意事项

- **仅支持 HotSpot JVM**（Oracle / OpenJDK / Zulu 等衍生版），不支持 OpenJ9、GraalVM 等其他 JVM
- **仅支持 x64 Windows**
- **JDK 版本兼容性取决于 VMStructs 稳定性**——原作者测试过 Zulu 17、OpenJDK 8/17，不保证其他版本
- **目标方法必须可被解释执行**——如果方法被 JIT 内联到调用者中，hook 会失效（这也是为什么设置 `dont_inline` 和 `NO_COMPILE`）
- **回调在 JVM 解释器线程上执行**——不要在里面做耗时操作，不要在回调里阻塞
- **`jmethodID` 可能因 RetransformClasses 失效**——内部已处理重新获取
- **多次 hook 同一 i2i entry 会被去重**——多个方法共享 i2i entry 时只放一个 hook，在 `common_detour` 里按 method 匹配分发

---

## 11. 和原版 Ripterms 的 API 差异

| 操作 | 原版 `Ripterms::JavaHook` | 新库 `JavaHook` |
|------|---------------------------|-----------------|
| 初始化 | 隐式（依赖 `Ripterms::p_tienv`） | 显式 `init()` |
| 注册 | `hook(mid, cb)` — 隐式全局变量 | `hook(tienv, env, mid, cb)` — 全显式 |
| 参数提取 | `get_jobject_param_at(frame, i)` | `get_jobject_param(frame, i)` |
| 头文件 | `"../Hook/JavaHook.h"` | `"JavaHook.h"` |
| 依赖 | 需要 `Ripterms.h` 全局变量 | **零外部全局依赖** |
