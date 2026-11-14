#pragma once

#include <cstdint>
#include <cstddef>

typedef size_t csh;

typedef enum cs_err
{
    CS_ERR_OK = 0,
    CS_ERR_MEM,
    CS_ERR_ARCH,
    CS_ERR_HANDLE,
    CS_ERR_CSH,
    CS_ERR_MODE,
    CS_ERR_OPTION,
    CS_ERR_DETAIL,
    CS_ERR_MEMSETUP,
    CS_ERR_VERSION,
    CS_ERR_DIET,
    CS_ERR_SKIPDATA,
    CS_ERR_X86_ATT,
    CS_ERR_X86_INTEL,
    CS_ERR_CANNOT_REPEAT
} cs_err;

enum cs_arch
{
    CS_ARCH_ARM = 0,
    CS_ARCH_ARM64,
    CS_ARCH_MIPS,
    CS_ARCH_X86,
    CS_ARCH_PPC,
    CS_ARCH_SPARC,
    CS_ARCH_SYSZ,
    CS_ARCH_XCORE,
    CS_ARCH_M68K,
    CS_ARCH_TMS320C64X,
    CS_ARCH_M680X,
    CS_ARCH_EVM,
    CS_ARCH_MOS65XX,
    CS_ARCH_WASM,
    CS_ARCH_BPF,
    CS_ARCH_RISCV,
    CS_ARCH_AARCH64 = CS_ARCH_ARM64
};

enum cs_mode
{
    CS_MODE_LITTLE_ENDIAN = 0,
    CS_MODE_ARM = 0,
    CS_MODE_16 = 1 << 1,
    CS_MODE_32 = 1 << 2,
    CS_MODE_64 = 1 << 3,
    CS_MODE_THUMB = 1 << 4,
    CS_MODE_MCLASS = 1 << 5,
    CS_MODE_V8 = 1 << 6
};

enum cs_opt_type
{
    CS_OPT_SYNTAX = 1,
    CS_OPT_DETAIL,
    CS_OPT_MODE,
    CS_OPT_MEM
};

enum cs_opt_value
{
    CS_OPT_OFF = 0,
    CS_OPT_ON = 3
};

enum cs_group_type
{
    CS_GRP_INVALID = 0,
    CS_GRP_JUMP,
    CS_GRP_CALL,
    CS_GRP_RET,
    CS_GRP_INT,
    CS_GRP_IRET,
    CS_GRP_PRIVILEGE,
    CS_GRP_BRANCH_RELATIVE
};

enum x86_op_type
{
    X86_OP_INVALID = 0,
    X86_OP_REG,
    X86_OP_IMM,
    X86_OP_MEM
};

enum arm_op_type
{
    ARM_OP_INVALID = 0,
    ARM_OP_REG,
    ARM_OP_IMM,
    ARM_OP_MEM,
    ARM_OP_FP,
    ARM_OP_CIMM,
    ARM_OP_PIMM,
    ARM_OP_SETEND,
    ARM_OP_SYSREG
};

enum aarch64_op_type
{
    AARCH64_OP_INVALID = 0,
    AARCH64_OP_REG,
    AARCH64_OP_IMM,
    AARCH64_OP_MEM,
    AARCH64_OP_FP,
    AARCH64_OP_CIMM,
    AARCH64_OP_REG_MRS,
    AARCH64_OP_REG_MSR,
    AARCH64_OP_PSTATE,
    AARCH64_OP_SYS,
    AARCH64_OP_PREFETCH,
    AARCH64_OP_BARRIER
};

struct cs_opt_x86
{
    x86_op_type type;
    int64_t imm;
};

struct cs_opt_arm
{
    arm_op_type type;
    int32_t imm;
};

struct cs_opt_aarch64
{
    aarch64_op_type type;
    int64_t imm;
};

struct cs_detail
{
    uint8_t groups[8];
    uint8_t groups_count;
    struct
    {
        uint8_t op_count;
        cs_opt_x86 operands[8];
    } x86;
    struct
    {
        uint8_t op_count;
        cs_opt_arm operands[8];
    } arm;
    struct
    {
        uint8_t op_count;
        cs_opt_aarch64 operands[8];
    } aarch64;
};

struct cs_insn
{
    uint32_t id;
    uint64_t address;
    uint16_t size;
    uint8_t bytes[16];
    char mnemonic[32];
    char op_str[160];
    cs_detail* detail;
};

inline cs_err cs_open(cs_arch, cs_mode, csh*) { return CS_ERR_ARCH; }
inline cs_err cs_close(csh*) { return CS_ERR_OK; }
inline cs_err cs_option(csh, cs_opt_type, size_t) { return CS_ERR_OK; }
inline size_t cs_disasm(csh, const uint8_t*, size_t, uint64_t, size_t, cs_insn**) { return 0; }
inline void cs_free(cs_insn*, size_t) {}
