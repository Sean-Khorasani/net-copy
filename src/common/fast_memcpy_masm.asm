; fast_memcpy_masm.asm
; Optimized memcpy and memset for Windows x64 using AVX2
; For VS2022 / MASM (ml64)

.code

; void* fast_memcpy_avx2_masm(void* dest, const void* src, size_t n)
; rcx = dest, rdx = src, r8 = count
fast_memcpy_avx2_masm PROC
    mov rax, rcx              ; Save dest for return
    mov r9, r8                ; Save count
    
    ; Handle small copies
    cmp r8, 128
    jb small_copy
    
    ; Large copy with AVX2 (32-byte chunks)
    mov r10, r8
    shr r10, 5                ; Divide by 32 for number of iterations
    
align_loop:
    vmovdqu ymm0, ymmword ptr [rdx]
    vmovdqu ymmword ptr [rcx], ymm0
    add rdx, 32
    add rcx, 32
    dec r10
    jnz align_loop
    
    vzeroupper
    
    ; Handle remaining bytes
    and r8, 31                ; r8 = count % 32
    jz done
    
    ; Copy remaining bytes (no recalculation needed, rcx/rdx are already at the tail)
    
tail_loop:
    mov r10b, byte ptr [rdx]
    mov byte ptr [rcx], r10b
    inc rdx
    inc rcx
    dec r8
    jnz tail_loop
    jmp done
    
small_copy:
    ; For small copies, use simple byte loop
    test r8, r8
    jz done
small_loop:
    mov r10b, byte ptr [rdx]
    mov byte ptr [rcx], r10b
    inc rdx
    inc rcx
    dec r8
    jnz small_loop
    
done:
    ret
fast_memcpy_avx2_masm ENDP

; void* fast_memset_avx2_masm(void* dest, int value, size_t n)
; rcx = dest, rdx = value (only dl used), r8 = count
fast_memset_avx2_masm PROC
    mov rax, rcx              ; Save dest for return
    mov r9, r8                ; Save count
    
    ; Broadcast byte value to ymm0
    movd xmm0, edx
    vpbroadcastb ymm0, xmm0
    
    ; Handle small sets
    cmp r8, 128
    jb small_set
    
    ; Large set with AVX2 (32-byte chunks)
    mov r10, r8
    shr r10, 5                ; Divide by 32
    
set_loop:
    vmovdqu ymmword ptr [rcx], ymm0
    add rcx, 32
    dec r10
    jnz set_loop
    
    vzeroupper
    
    ; Handle remaining bytes
    and r8, 31
    jz set_done
    
    ; Set remaining bytes
    mov rcx, rax
    add rcx, r9
    sub rcx, r8
    
set_tail:
    mov byte ptr [rcx], dl
    inc rcx
    dec r8
    jnz set_tail
    jmp set_done
    
small_set:
    test r8, r8
    jz set_done
small_set_loop:
    mov byte ptr [rcx], dl
    inc rcx
    dec r8
    jnz small_set_loop
    
set_done:
    ret
fast_memset_avx2_masm ENDP

END
