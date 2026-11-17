.text
.globl _ReturnAddress
_ReturnAddress:
    movq (%rsp), %rax
    ret
