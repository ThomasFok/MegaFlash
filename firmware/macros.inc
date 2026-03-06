.ifndef _MACROS_INC
_MACROS_INC = 1

.define         EQU             =
.define         TRUE            1
.define         FALSE           0

.macro bge arg ; branch on greater-than or equal
	bcs arg
.endmacro

.macro blt arg ; branch on less-than
	bcc arg
.endmacro

.macro txy
	phx
	ply
.endmacro

.macro tyx
	phy
	plx
.endmacro

;Move 16-bit from memory to memory
.macro mov16 dest,src
	lda src
	sta dest
	lda src+1
	sta dest+1
.endmacro

;Load 16-bit immediate value to memory destination
.macro ld16i dest,immed
	.if .LOBYTE(immed)=0
					stz dest
	.else
					lda #.LOBYTE(immed)
					sta dest
	.endif
	.if .HIBYTE(immed)=0
					stz dest+1
	.else
					lda #.HIBYTE(immed)
					sta dest+1
	.endif                
.endmacro

;Load 16-bit immediate value to AY (A=LOBYTE, Y=HIBYTE)
.macro ld16iay immed
	lda #.LOBYTE(immed)
	ldy #.HIBYTE(immed)
.endmacro


.macro skip1
	.byte $24	;opcode of bit zp, to skip the next 1 byte instruction
.endmacro

.macro skip2
	.byte $2c	;opcode of bit absolute, to skip the next 2 bytes instruction
.endmacro


;ASCII String with MSB off
.macro  lascii  s
	.repeat .strlen(s), i
	.byte   .strat(s,i) & $7f
	.endrepeat
.endmacro

;ASCII String with MSB on
.macro  hascii  s
	.repeat .strlen(s), i
	.byte   .strat(s,i) | $80
	.endrepeat
.endmacro

;.asciiz with MSB on
.macro  hasciiz  s
	.repeat .strlen(s), i
	.byte   .strat(s,i) | $80
	.endrepeat
	.byte $00
.endmacro


.endif



