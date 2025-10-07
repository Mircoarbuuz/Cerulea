 SECTION .text
GLOBAL _start
_start:
    MOV RAX, 1
    JMP .LOOP 
.LOOP:
    JMP .LOOP