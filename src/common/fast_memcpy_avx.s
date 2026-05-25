# fast_memcpy_avx.s
# Hand-tuned AVX2 memcpy example for x86-64 (Linux gas syntax)
# Assemble with: gcc -c fast_memcpy_avx.s or as
# For max performance in file buffer copies / encryption prep.
# This is a simplified high-performance version using AVX2.

.text
.globl fast_memcpy_avx2
.type fast_memcpy_avx2, @function
fast_memcpy_avx2:
    # rdi = dest, rsi = src, rdx = n (bytes)
    movq %rdi, %rax          # return dest
    cmpq $128, %rdx
    jb .Lsmall_copy

    # Align destination to 32 bytes if possible
    movq %rdi, %rcx
    andq $31, %rcx
    jz .Laligned
    # Handle unaligned prefix (simple byte copy for simplicity in this example)
    movq $32, %r8
    subq %rcx, %r8
    cmpq %r8, %rdx
    cmovb %rdx, %r8
    subq %r8, %rdx
    rep movsb                # or manual loop; simplified here
    # For production, use better prefix handling

.Laligned:
    # Main AVX2 loop - 32 bytes at a time (or 64 with two YMM)
    movq %rdx, %r8           # Save remaining size in %r8
    shrq $5, %rdx            # number of 32-byte chunks
    jz .Ltail

.Lavx_loop:
    vmovdqu (%rsi), %ymm0
    vmovdqu %ymm0, (%rdi)
    addq $32, %rsi
    addq $32, %rdi
    decq %rdx
    jnz .Lavx_loop

    vzeroupper

.Ltail:
    # Handle remaining bytes
    andq $31, %r8            # %r8 = remainder
    movq %r8, %rcx           # Set %rcx to remainder for rep movsb
    # Simplified tail: use rep movsb for remaining
    rep movsb

    ret

.Lsmall_copy:
    movq %rdx, %rcx
    rep movsb
    ret
