    .section .rodata
    .balign 4
    .global initramfs_start, initramfs_end
initramfs_start:
    .incbin INITRAMFS
initramfs_end:
