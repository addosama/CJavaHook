#pragma once

#include <cstdint>
#include <string>
#include <JNI/jni.h>

namespace HotSpot
{
    bool init();

    typedef struct {
        const char* typeName;
        const char* fieldName;
        const char* typeString;
        int32_t  isStatic;
        uint64_t offset;
        void* address;
    } VMStructEntry;

    typedef struct {
        const char* typeName;
        const char* superclassName;
        int32_t isOopType;
        int32_t isIntegerType;
        int32_t isUnsigned;
        uint64_t size;
    } VMTypeEntry;

    typedef struct {
        const char* name;
        int32_t value;
    } VMIntConstantEntry;

    typedef struct {
        const char* name;
        uint64_t value;
    } VMLongConstantEntry;

    typedef struct {
        const char* name;
        void* value;
    } VMAddressEntry;

    enum Flags
    {
        _caller_sensitive       = 1 << 0,
        _force_inline           = 1 << 1,
        _dont_inline            = 1 << 2,
        _hidden                 = 1 << 3,
        _has_injected_profile   = 1 << 4,
        _intrinsic_candidate    = 1 << 5,
        _reserved_stack_access  = 1 << 6,
        _scoped                 = 1 << 7
    };

    enum
    {
        JVM_ACC_WRITTEN_FLAGS           = 0x00007FFF,
        JVM_ACC_MONITOR_MATCH           = 0x10000000,
        JVM_ACC_HAS_MONITOR_BYTECODES   = 0x20000000,
        JVM_ACC_HAS_LOOPS               = 0x40000000,
        JVM_ACC_LOOPS_FLAG_INIT         = (int)0x80000000,
        JVM_ACC_QUEUED                  = 0x01000000,
        JVM_ACC_NOT_C2_COMPILABLE       = 0x02000000,
        JVM_ACC_NOT_C1_COMPILABLE       = 0x04000000,
        JVM_ACC_NOT_C2_OSR_COMPILABLE   = 0x08000000,
        JVM_ACC_HAS_LINE_NUMBER_TABLE   = 0x00100000,
        JVM_ACC_HAS_CHECKED_EXCEPTIONS  = 0x00400000,
        JVM_ACC_HAS_JSRS                = 0x00800000,
        JVM_ACC_IS_OLD                  = 0x00010000,
        JVM_ACC_IS_OBSOLETE             = 0x00020000,
        JVM_ACC_IS_PREFIXED_NATIVE      = 0x00040000,
        JVM_ACC_ON_STACK                = 0x00080000,
        JVM_ACC_IS_DELETED              = 0x00008000,
        JVM_ACC_HAS_MIRANDA_METHODS     = 0x10000000,
        JVM_ACC_HAS_VANILLA_CONSTRUCTOR = 0x20000000,
        JVM_ACC_HAS_FINALIZER           = 0x40000000,
        JVM_ACC_IS_CLONEABLE_FAST       = (int)0x80000000,
        JVM_ACC_HAS_FINAL_METHOD        = 0x01000000,
        JVM_ACC_IS_SHARED_CLASS         = 0x02000000,
        JVM_ACC_IS_HIDDEN_CLASS         = 0x04000000,
        JVM_ACC_IS_VALUE_BASED_CLASS    = 0x08000000,
        JVM_ACC_HAS_LOCAL_VARIABLE_TABLE = 0x00200000,
        JVM_ACC_PROMOTED_FLAGS          = 0x00200000,
        JVM_ACC_FIELD_ACCESS_WATCHED         = 0x00002000,
        JVM_ACC_FIELD_MODIFICATION_WATCHED   = 0x00008000,
        JVM_ACC_FIELD_INTERNAL               = 0x00000400,
        JVM_ACC_FIELD_STABLE                 = 0x00000020,
        JVM_ACC_FIELD_INITIALIZED_FINAL_UPDATE = 0x00000100,
        JVM_ACC_FIELD_HAS_GENERIC_SIGNATURE  = 0x00000800,
        JVM_ACC_FIELD_INTERNAL_FLAGS         = JVM_ACC_FIELD_ACCESS_WATCHED |
                                               JVM_ACC_FIELD_MODIFICATION_WATCHED |
                                               JVM_ACC_FIELD_INTERNAL |
                                               JVM_ACC_FIELD_STABLE |
                                               JVM_ACC_FIELD_HAS_GENERIC_SIGNATURE,
        JVM_ACC_PUBLIC       = 0x0001,
        JVM_ACC_PRIVATE      = 0x0002,
        JVM_ACC_PROTECTED    = 0x0004,
        JVM_ACC_STATIC       = 0x0008,
        JVM_ACC_FINAL        = 0x0010,
        JVM_ACC_SYNCHRONIZED = 0x0020,
        JVM_ACC_SUPER        = 0x0020,
        JVM_ACC_VOLATILE     = 0x0040,
        JVM_ACC_BRIDGE       = 0x0040,
        JVM_ACC_TRANSIENT    = 0x0080,
        JVM_ACC_VARARGS      = 0x0080,
        JVM_ACC_NATIVE       = 0x0100,
        JVM_ACC_INTERFACE    = 0x0200,
        JVM_ACC_ABSTRACT     = 0x0400,
        JVM_ACC_STRICT       = 0x0800,
        JVM_ACC_SYNTHETIC    = 0x1000,
        JVM_ACC_ANNOTATION   = 0x2000,
        JVM_ACC_ENUM         = 0x4000
    };

    enum JavaThreadState
    {
        _thread_uninitialized  = 0,
        _thread_new            = 2,
        _thread_new_trans      = 3,
        _thread_in_native      = 4,
        _thread_in_native_trans = 5,
        _thread_in_vm          = 6,
        _thread_in_vm_trans    = 7,
        _thread_in_Java        = 8,
        _thread_in_Java_trans  = 9,
        _thread_blocked        = 10,
        _thread_blocked_trans  = 11,
        _thread_max_state      = 12
    };

    struct DirectoryEntry
    {
        DirectoryEntry* get_next();
        void* get_literal();
    };

    struct Dictionary
    {
        DirectoryEntry** get_buckets();
        int get_table_size();
    };

    struct Symbol
    {
        std::string to_string();
    };

    struct ConstantPool
    {
        void** get_base();
        static int get_size();
        int get_length();
    };

    struct ConstMethod
    {
        ConstantPool* get_constants();
        void set_constants(ConstantPool* _constants);
        unsigned short get_name_index();
        unsigned short get_signature_index();
    };

    struct AccessFlags
    {
        jint _flags;
        bool is_static();
    };

    struct Method
    {
        ConstMethod* get_constMethod();
        std::string get_signature();
        std::string get_name();
        int get_parameters_count();
        AccessFlags* get_access_flags();
        void* get_from_interpreted_entry();
        void set_from_interpreted_entry(void* entry);
        void* get_from_compiled_entry();
        void set_from_compiled_entry(void* entry);
        void* get_i2i_entry();
        unsigned short* get_flags();
        void set_dont_inline(bool enabled);
    };

    struct Array
    {
        int get_length();
        void** get_data();
    };

    struct Array_u2
    {
        int get_length();
        unsigned short* get_data();
    };

    struct Thread
    {
        JNIEnv* get_env();
        uint32_t get_suspend_flags();
        JavaThreadState get_thread_state();
        void set_thread_state(JavaThreadState state);
        static int get_thread_state_offset();
    };

    struct Klass
    {
        Symbol* get_name();
        Method* findMethod(const std::string& method_name, const std::string& method_sig);
        Array_u2* get_fields();
        Array* get_methods();
        ConstantPool* get_constants();
    };

    struct FieldInfo
    {
        unsigned short _data[6];
        unsigned short get_name_index();
        unsigned short get_signature_index();
        unsigned short get_access();
        bool is_internal();
        bool is_public();
        bool is_private();
        bool is_protected();
        bool is_static();
        bool is_final();
    };

    struct frame
    {
        inline static int locals_offset = -56;
        void** get_locals();
        Method* get_method();
    };
};
