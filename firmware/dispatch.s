;--------------------------------------------------------
; Apple IIc MegaFlash Firmware
; Module: Dispatcher
;

                .setcpu "65C02"
                .segment "DISPATCH"
                .reloc
                
                .include "buildflags.inc"                
                .include "defines.inc"
                .include "macros.inc"                   

                ;
                ; Imports
                ;

                ;Handlers from other modules
                .import p8spentry,copybc,coldstartinit,clockdriverimpl
                .import loadcpanel
                .import copybm
                
                ;
                ; Exports
                ;
        
                
;---------------------------------------------------------------------------------     
; slxeq $C752 is the original entry point of Slinky firmware.
; This entry point is used to execute MegaFlash command from main ROM Bank.
; But the original implementation use screen hole ($678) as temporary storage.
; For Total Replay, games may not follow the rule and they may use screen hole
; area. So, we use our own implementation so that all memory location are preserved

              
                ;
                ;Memory Allocation
                ;
                .segment "ZEROPAGE":zeropage
                .reloc
                .exportzp aval,xval,yval,lcstate
aval:          .res 1
xval:          .res 1
yval:          .res 1
lcstate:       .res 1                


                ;
                ;Hook to original slxeq routine
                ;
                ;SLXEQHOOK is at $C755 of Bank 1
                .segment "SLXEQHOOK"
                .reloc
                jmp slxeqx      ;Branch to our own implementation
                ;Note: There are 14 bytes of free space here
                ;We may make use of them if running out of memory

                ;
                ;Our own implementation
                ;It must be in IOROM segment since Language Card
                ;area may be switched to RAM.
                ;
                .segment "IOROM"
                .reloc               
slxeqx:
                ;Step 1 Save aval,xval,yval,lcstate to stack
                ldy yval
                phy
                ldy xval
                phy
                ldy aval
                phy
                ldy lcstate
                phy

                ;Step 2 Get LC State
                ;It also switches LC Area to ROM
                jsr getlc       ;A and X remains unchanged
                sty lcstate     ;Save it to lcstate

                ;Step 3 Execute the command by calling dispatcher
                jsr dispatch
                
                ;Step 4 Restore Language Card and lcstate
                ldx lcstate
                inc $c000,x
                pla
                sta lcstate
                
                ;Step 5 Restore aval,xval,yval and load return values to 
                ;A,X and Y registers
                ;The original values of aval, xval, yval is in the stack
                ;aval, xval, yval are currently holding the returns value
                ;We need to move the return values to A,X and Y registers
                ;and restore the original values of aval, xval and yval
                ;without using any memory locations.
                
                ;Current Stack:
                ;
                ;    SP ->
                ;Offset +1: original aval
                ;Offset +2: original xval
                ;offset +3: original yval
                
                tsx             ;X=SP
                
                ;Swap SP+3 and yval
                ;so that yval is restored and the Y return value
                ;is in the stack at offset +3
                ldy $103,x      ;Get original value of yval
                lda yval        ;Get Y return value
                sty yval        ;Restore original value of yval
                sta $103,x      ;Put Y return value to SP+3
                
                ;SP+2 -> xval
                ;xval -> x
                ;Now, X register holds the X return value
                ;xval is restored.
                ldy $102,x      ;Get original value of xval
                ldx xval        ;Get X return value to X Register
                sty xval        ;Restore original value of xval
                
                ;SP+1 -> aval
                ;aval -> a
                ;Now, A register holds the A return value
                ;aval is restored.
                ply             ;pop the stack to get original value of aval
                lda aval        ;Get A return value to A Register
                sty aval        ;Restore original value of aval
                
                ply             ;discard the dummy byte from stack
                ply             ;Get Y return value
                jmp swrts
                
                
;---------------------------------------------------------------------------------
;When slxeq is called, the program flow will eventually reach
;this routine. The A and X registers remains unchanged.
;The A register is the operation mode.
;It dispatches to handlers according to mode.
;Bit 7 and 6 of mode are ignored so that additional information
;can be passed to handler using these two bits.        
                .segment "ROM1"
                .reloc
 
dispatch:
                ;Store a and x to aval, xval so handlers can get them
                sta aval
                stx xval
                and #%00111111          ;A=mode, Mask out bit 7 and 6
                cmp #JMPTBLLEN
                blt modeok
known_rts:      rts
                        
                ;Dispatch to handler
modeok:         asl                     ; times 2
                tax
                jmp (jmptable,x)
                

jmptable:                       
                .addr p8spentry         ; 0
                .addr copybc            ; 1
                .addr coldstartinit     ; 2
                .addr clockdriverimpl   ; 3
                .addr loadcpanel        ; 4
                .addr copybm            ; 5        
JMPTBLLEN       = (*-jmptable)/2        ;No of entries of jmptable





