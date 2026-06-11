;--------------------------------------------------------
; Apple IIc MegaFlash Firmware
; Module: Dispatcher
;

                ;All codes default to this segment
                .define HOMESEGMENT "ROM1"

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
        
                
;***********************************************************
;
; SLXEQ Hook
;
; slxeq $C752 is the original entry point of Slinky firmware
; It switches the ROM to aux bank and continue from address
; $C755.
;
; But the original implementation does not meet our requirement.
; So, we replace it with our own implementation.
;
; Calling Convention
; ==================
;
; To call a handler through slxeq,
;
; Load the handler ID (aka mode) to A-register
; Load any optional parameter to X-register
; Then, call slxeq routine
;
; The slxeq routine will eventually call the handler in aux
; ROM bank.
;
; The handler can retrieve the A and X register values through
; location aval and xval.
;
; To return values back to the caller, put the return values
; to location aval, xval and yval. Then, execute RTS. Those 
; values will be loaded to A,X and Y registers and returned 
; to the caller.
;
;***********************************************************

              
;
;Zero Page Memory Allocation
;These memory addresses are not dedicated to MegaFlash.
;Their values must be restored.
;
                .segment "ZEROPAGE":zeropage
                .reloc
                .exportzp aval,xval,yval,lcstate
aval:          .res 1
xval:          .res 1
yval:          .res 1
lcstate:       .res 1                

;*********************************************************
;
;Hook to original slxeq routine
;
;SLXEQHOOK is at $C755 of Bank 1
;There are 14 bytes of space from $C755 to $C762.
;Then, we continue in HOMESEGMENT segment
;                
                
                .segment "SLXEQHOOK"   
                .reloc
               
                ;Step 1 Save xval,lcstate to stack
                ldy xval
                phy
                ldy lcstate
                phy
                
                ;Step 2 Get LC State
                ;It also switches LC Area to ROM
                jsr getlc       ;A and X remains unchanged. return LC setting in Y           
                sty lcstate     ;Save it to lcstate

                jmp slxeq2
                ;then, continue in HOMESEGMENT segment

                .segment HOMESEGMENT
                .reloc               
slxeq2:         ;Step 3 Save yval,aval to stack
                ldy yval
                phy
                ldy aval
                phy

                ;Step 4 Execute the command by calling dispatcher
                jsr dispatch
                
                ;Step 5 Restore aval,xval,yval and load return values to 
                ;A,X and Y registers
                ;The original values of aval, xval, yval is in the stack
                ;aval, xval, yval are currently holding the return values
                ;We need to move the return values to A,X and Y registers
                ;and restore the original values of aval, xval and yval
                ;without using any memory locations.
                
                ;Current Stack:
                ;
                ;    SP ->
                ;Offset +1: original aval
                ;Offset +2: original yval
                ;Offset +3: original lcstate
                ;offset +4: original xval
                
                tsx             ;X=SP
                
                ;Swap SP+4 and xval
                ;so that xval is restored and the X return value
                ;is at SP+4
                ldy $104,x      ;Get original value of xval
                lda xval        ;Get X return value
                sty xval        ;Restore original value of xval
                sta $104,x      ;Put X return value to SP+4
                
                ;SP+2 -> yval
                ;yval -> y
                ;Now, Y register holds the Y return value
                ;yval is restored.
                lda $102,x      ;Get original value of yval
                ldy yval        ;Get Y return value to Y Register
                sta yval        ;Restore original value of yval
                ;Don't change Y register after this point
                
                ;Current Stack:
                ;
                ;    SP ->
                ;Offset +1: original aval
                ;Offset +2: original yval (not needed anymore)
                ;Offset +3: original lcstate                
                ;offset +4: x return value            
                
                ;SP+1 -> aval
                ;aval -> a
                ;Now, A register holds the A return value
                ;aval is restored.
                plx             ;pop the stack to get original value of aval
                lda aval        ;Get A return value to A Register
                stx aval        ;Restore original value of aval
                plx             ;discard the original yval from stack
                ;Don't change A and Y registers after this point

                ;Current Stack:
                ;
                ;    SP ->
                ;Offset +1: original lcstate                
                ;offset +2: x return value     

                ;Step 6 Restore Language Card and lcstate
                ldx lcstate     ;Get LC setting
                jmp slxeq3    
                ;continue in IOROM segment to restore LC setting
                
                .segment "IOROM"
                .reloc
slxeq3:         inc $c000,x     ;Restore LC setting
                plx             ;Restore lcstate location
                stx lcstate     ;
                
                ;Finally, Get X return value
                plx             ;Get X return value
                jmp swrts       ;Switch Bank and return to the caller
                                ;Don't use fswrts since LC setting is restored.
                

                
;---------------------------------------------------------------------------------
;When slxeq is called, the program flow will eventually reach
;this routine. The A and X registers remains unchanged.
;The A register is the operation mode.
;It dispatches to handlers according to mode.
;Bit 7 and 6 of mode are ignored so that additional information
;can be passed to handler using these two bits.        
                .segment HOMESEGMENT
                .reloc
 
dispatch:       ;Store a and x to aval, xval so handlers can get them
                sta aval
                stx xval
                and #%00111111          ;A=mode, Mask out bit 7 and 6
                cmp #JMPTBLLEN
                blt modeok
                rts
                        
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




