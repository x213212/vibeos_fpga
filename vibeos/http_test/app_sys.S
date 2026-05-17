.section .text
.global app_ecall
.type app_ecall, @function
app_ecall:
    mv a7, a3
    ecall
    ret
