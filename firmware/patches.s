;--------------------------------------------------------
; Apple IIc MegaFlash Firmware
; Module: System ROM Patches

                .setcpu "65C02"
                .segment "ROM1"
                .reloc

                .include "buildflags.inc"
                .include "defines.inc"
                .include "macros.inc"

                
                ;
                ; Imports
                ;
                
                ;
                ;Exports
                ; 
                .export fswrts
                
;----------------------------------------------------
; Cold Start Initialization
; We need to intercept the cold start routine (Power up or 
; Ctrl-OA-Reset) to initialize the driver and hardware.
;
; The code of stock firmware at $FB19 is 
; FB19: JMP ($0000)
;       BRK
;       BRK
; 
; It is the last instruction of Power Up routine. ($00) should
; points to $C400. The jmp instruction starts the boot sequence.
;
; We intercept this instruction and call our routine which is in auxrom.
; There are two BRK instruction after the JMP instruction. There are 5 Bytes
; of empty space which is enough to jump to our routine
;

                .segment "B0_FB19" ;Size=5 Bytes
                .reloc
                lda #MODE_INIT  ;Run coldstartinit in aux bank
                jmp slxeq       ;Jump to our routine, no return

;-----------------------------------------------------
; FSWRTS - Switch from Aux ROM Bank to Main Bank
;
; The SWRTS routine is to switch ROM Bank and then a RTS
; instruction. The routine is at $C784, which is non-cacheable
; on IIc plus/ZIP Chip.
; 
; There is a RTS instruction at $FFCB in bank 0. If we put
; a ROM switch instruction right before it in bank 1, we can 
; put a SWRTS routine in cacheable region.
;
;       Bank 1         Bank 0
; FFC8: sta rombank
; FFCB:                rts
;
                .segment "B1_FFC8";
                .reloc
fswrts:         sta rombank
                ;rts at $FFCB in bank0
                
                
.ifdef IICP     ;For Apple IIc Plus only
;-----------------------------------------------------
;Remove ROM Checksum test (Apple IIC+ only)
;
;Note: ROM checksum routine at $D049 in aux bank
;
                .segment "B1_C53D"
                nop
                nop
                clc


;----------------------------------------------------
;Original Wait Routine for bell fix
;
                .segment "B0_C755"
                .reloc       
orgwait:        sec
orgwait2:       pha
orgwait3:       sbc #$01
                bne orgwait3
                pla
                sbc #$01
                bne orgwait2
                rts
                
;----------------------------------------------------
;Bell Fix - call orgwait instead of wait in ROM
;
                .segment "B0_FBDD"
                .reloc
bell1:          lda #$40        ;Delay 0.01 Seconds
                jsr wait        ;Not related to Sound Generation
                                ;Keep the original code
                
                ldy #$c0
bell2:          lda #$0c
                jsr orgwait     ;Use Original Wait routine
                lda spkr
                dey
                bne bell2
                rts
                
                .assert *-bell1=19, error, "length of bell1 not 19"

                
;----------------------------------------------------
;Center Title - Center "Apple IIc +" on the screen
;
                .segment "B0_FB68"
                .reloc
                sta $040e,y
                
.endif ;.ifdef IICP

                
;----------------------------------------------------
;Applesoft ONERR GOTO Bug Fix
;
; If this program is executed on stock firmware, it
; crashes after several iterations.
;
; 10 ONERR GOTO 100
; 20 I = 2
; 30 I = I - 1
; 40 PRINT 6/I
; 50 GOTO 30
; 100 I= 2
; 110 GOTO 40
;
;The bug is ONERR GOTO routine fails to reset the stack pointer.
;So, each time ONERR GOTO is triggered, it leaves some gabbage 
;in the stack. 
;
;The fix is to setup stack pointer before jump to $D7D2 at $F315.
;Luckily, the required code sequence to fix the problem already
;exists at $F328. We just need to change JMP $D7D2 to JMP $F328
;at $F315.
;
;The Original Code at $F315 and F328
;F315: JMP D7D2
;
;F328: LDX $DF
;F32A: TXS
;F32B: JMP $D7D2
;

.if ONERR_FIX
                .segment "B0_F315"
                .reloc
                jmp $f328
.endif
             

;----------------------------------------------------
;Applesoft PRINT Speed Fix
;
;If this program is executed on Apple IIc plus
; 10 PRINT "HELLO"
; 20 GOTO 10
;
; The speeds at 4MHz and at 1MHz are almost identical. 
;
; The problem is the SPEED command. After a character
; is printed to screen, the wait routine is called even
; SPEED is set to 255. ($DB6C: JSR $FCA8)
;
; On IIc plus, the wait routine touches the slot 1 and
; the CPU will slow down to 1MHz for 50ms. If a program
; print to screen continuously, the whole program is
; executed at 1MHz
;
; Two Fixes are implemented here
; 1) Since we have the original wait routine for bell fix,
;    just call the original wait instead of wait routine 
;    in monitor.
; 2) A free memory space at $C1DB in bank 0 is discovered.
;    So, we may implement additional code. The delay
;    is minimum when A=1. We just skip the calling of wait
;    routine to further improve printing speed.
;    
;
.if .defined(IICP) .and PRINTSPEED_FIX
                .segment "B0_DB6C"
                .reloc
                .if 0   ;Patch to call orgwait is enough to fix the problem
                jsr orgwait     
                .else   ;A even better implementation
                jsr printwait

                .segment "APPLESOFT"
                .reloc
printwait:      ;A=Delay Duration
                ;Skip calling wait if A=1 (Min delay)
                cmp #$01
                bne :+
                rts
:               jmp orgwait     ;jmp=jsr+rts
                .endif
.endif
      
;----------------------------------------------------
;Applesoft Integer Variable Conversion Fix
;
;The range of integer variable is 32767  to -32768
;
;If this sequence of instructions are executed
;   A=-32768: A%=A: PRINT A%
;
;The result should be -32768. But Illegal Quantity Error
;occurs
;
;A constant with value -32768 in MBF format is located 
;at $E0FE. It should be $90,$80,$00,$00,$00. But
;the last $00 byte is missing. So, the value of the constant
;becomes -32678.00049 
;
;The fix is to provide the correct constant and patch the code
;to use it.
.if INT_FIX  
  
                .segment "APPLESOFT"
                .reloc
NEG32768:       .byte $90,$80,$00,$00,$00       ;-32768 in MBF format    
      
                .segment "B0_E112"
                .reloc
                lda #<NEG32768
                ldy #>NEG32768
.endif      
      
      
      
      
      