/* Native fixture callers for callee-saved registers and a 4 KiB guest slice.
   Production hle_stack.S performs the stack switch under test. */
.text
.p2align 2
.globl _fixture_call_sentinels
_fixture_call_sentinels:
    stp x29, x30, [sp, #-160]!
    stp x19, x20, [sp, #16]
    stp x21, x22, [sp, #32]
    stp x23, x24, [sp, #48]
    stp x25, x26, [sp, #64]
    stp x27, x28, [sp, #80]
    stp d8, d9, [sp, #96]
    stp d10, d11, [sp, #112]
    stp d12, d13, [sp, #128]
    stp d14, d15, [sp, #144]
    mov x29, sp
    mov x16, x0
    mov x0, x1
    mov x1, x2
    mov x2, x3
    mov w3, w4
    mov x19, #0x1919
    mov x20, #0x2020
    mov x21, #0x2121
    mov x22, #0x2222
    mov x23, #0x2323
    mov x24, #0x2424
    mov x25, #0x2525
    mov x26, #0x2626
    mov x27, #0x2727
    mov x28, #0x2828
    mov x9, #0xd808
    fmov d8, x9
    mov x9, #0xd909
    fmov d9, x9
    mov x9, #0xda10
    fmov d10, x9
    mov x9, #0xdb11
    fmov d11, x9
    mov x9, #0xdc12
    fmov d12, x9
    mov x9, #0xdd13
    fmov d13, x9
    mov x9, #0xde14
    fmov d14, x9
    mov x9, #0xdf15
    fmov d15, x9
    blr x16
    mov w0, #1
    mov x9, sp
    cmp x9, x29
    b.ne 1f
.macro check_integer reg, value
    mov x9, #\value
    cmp \reg, x9
    b.ne 1f
.endm
    check_integer x19, 0x1919
    check_integer x20, 0x2020
    check_integer x21, 0x2121
    check_integer x22, 0x2222
    check_integer x23, 0x2323
    check_integer x24, 0x2424
    check_integer x25, 0x2525
    check_integer x26, 0x2626
    check_integer x27, 0x2727
    check_integer x28, 0x2828
.macro check_float reg, value
    fmov x10, \reg
    mov x9, #\value
    cmp x10, x9
    b.ne 1f
.endm
    check_float d8, 0xd808
    check_float d9, 0xd909
    check_float d10, 0xda10
    check_float d11, 0xdb11
    check_float d12, 0xdc12
    check_float d13, 0xdd13
    check_float d14, 0xde14
    check_float d15, 0xdf15
    b 2f
1:  mov w0, #0
2:  ldp d14, d15, [sp, #144]
    ldp d12, d13, [sp, #128]
    ldp d10, d11, [sp, #112]
    ldp d8, d9, [sp, #96]
    ldp x27, x28, [sp, #80]
    ldp x25, x26, [sp, #64]
    ldp x23, x24, [sp, #48]
    ldp x21, x22, [sp, #32]
    ldp x19, x20, [sp, #16]
    ldp x29, x30, [sp], #160
    ret

.p2align 2
.globl _fixture_call_on_guest_stack
_fixture_call_on_guest_stack:
    stp x29, x30, [sp, #-32]!
    stp x19, x20, [sp, #16]
    mov x29, sp
    mov x19, sp
    mov x20, x0
    mov x16, x1
    mov x0, x2
    mov x1, x3
    mov x2, x4
    mov w3, w5
    mov sp, x20
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    blr x16
    ldp x29, x30, [sp], #16
    mov sp, x19
    ldp x19, x20, [sp, #16]
    ldp x29, x30, [sp], #32
    mov w0, #1
    ret
