@ There is no way to embed binary data to C program without conversion.
@ This assembly file is to include the romdisk.po file as binary data
@ using .incbin directive.
@
@ Two symbols are exported.
@ romdiskImage is the location of the data
@ romdiskImageLen is the length of the data


.cpu cortex-m0plus
.thumb

.section .rodata
.global romdiskImage
.global romdiskImageLen

@
@ romdisk.po binary data
@
        .align 4
romdiskImage:
        .incbin "romdisk.po"
romdiskImageEnd:

@
@ length of the data
@
        .align 4
romdiskImageLen:
        .long (romdiskImageEnd-romdiskImage)
