;--------------------------------------------------------
; Apple IIc MegaFlash Firmware
; Module: Applesoft FPU
;

                .setcpu "65C02"
                .reloc

                .include "buildflags.inc"
                .include "defines.inc"
                .include "macros.inc"
                .include "../common/defines.inc"
                
                ;
                ; Imports
                ;
                .import fpuenabled

                ;
                ; Exports
                ; 

;
; Applesoft Addresses
;
overflow        := $E8D5        ;Overflow Error Handler
zerodiv         := $EAE1        ;Division by zero Error Handler
iqerr           := $E199        ;Illegal Quantity Error Handler
fac             := $9D          ;fac location (6 bytes)
facext          := $AC          ;fac extenstion byte 
arg             := $A5          ;arg location (6 bytes)
stack           := $100         ;Bottom of stack


.if FPUSUPPORT

;*******************************************************************
; Calling Mechanism
;
; Requirement and Limitation
; 1) Falling back to original Applesoft Implementation if Megaflash
; does not exist or FPU Accelaration is disabled.
; 2) Lack of ROM Space. When we hook into Applesoft Floating Point
; routines, the only available memory space is the SLOTROM area ($C400-$C4FF)
; The space available is extremely limited.
;
; Let use FADD routine as an example to illustrate how it works.
;
; The first two instructions of Applesoft FADD routine are:
; E7C6: LDX $AC
; E7C8: STX $92
;
; The instructions are modified to
; E7C6: jsr fadd     fadd: ldy #CMD_FADD    
; E7C9: nop                jsr fpu_exec
;                          ldx $ac
;                          stx $92
;                          rts     
;
; When FADD is called,
; First, it goes through two JSR to reach fpu_exec
; Then, there are 3 possible situations.
;
; 1) The FPU is disabled or MegaFlash does not exists. To fallback to original
; Applesoft Implementation. fpu_exec simply does RTS to return to fadd. Then,
; the instructions originally at $E7C6 is executed. Finally, execute a RTS
; instruction to return to the original Applesoft implementation ($E7C9).
;
; 2) FPU implementation is used and there is no error. After fpu_exec does
; all its works, it pops two return addresses from stack. Finally, it executes a
; RTS instruction to return to the caller of Applesoft FADD routine.
;
; 3) FPU implementation is used and there is error such as Overflow Error or
; Division by Zero Error. fpu_exec does not return. It jumps to the error handler
; directly. This is how the Applesoft routine originally works. 
;
; As a result, fadd is just 10 bytes long. All error handling, branching are
; handled by fpu_exec.
;
; Stack when this function is called.
;    SP -->:
; Offset +1: return address of jsr fpu_exec low byte
; Offset +2: return address of jsr fpu_exec high byte
; Offset +3: return address of jsr fpu_xxxx low byte
; Offset +4: return address of jsr fpu_xxxx high byte  
; Offset +5: return address of the caller of FPU routine low byte
; Offset +6: return address of the caller of FPU routine high byte
  
  
;----------------------------------------------------     
; Execute FPU command
;
; Input: Y = FPU Command
;
; The actual routine is in Aux ROM Bank.
; There is a hole at $C7FC in both ROM bank. The location 
; can be used to switch from Main bank to Aux bank.
; So, we can move fpu_exec routine to Aux ROM bank without
; a lot of overhead.
; Thank you to ROM5X project!

; The fpu_exec routine is at $C7FC of main bank.
; After sta rombank instruction, the ROM bank is switched.
; Then, jmp to the actual routine.
;
; To return to main bank, jump to swrts routine instead of
; executing RTS instruction. swrts routine switch back to
; Main ROM bank and execute a RTS instruction.
;




                .segment "B0_C7FC"
                .reloc
fpu_exec:       sta rombank             ;First, Switch to Aux Bank

                ;We still have one byte of empty space at $C7FF.
                ;Use this byte as a signature to indicate FPU Support is enabled in firmware
                ;The Control Panel use this byte to detect FPU support
                .byte FPU_SUPPORT_SIGNATURE     
         
                .segment"B1_C7FF"
                .reloc
                jmp fpu_exec2           ;Then, jump to the actual routine in Aux Bank
;---------------------                

                .segment "FPU"
                .reloc
fpu_exec2:      bit fpuenabled  ;Test if FPU is enabled
                bpl userom      ;Branch if disabled

                ;Test if Megaflash exists
                ;Always test if MegaFlash exists and does not cache
                ;the result since fpuenabled flag is not reliable.
                ;Some codes may overwrite the flag or cache byte.
                ;We need to make sure Applesoft works in all circumstances.
                ;The sub-routines like chkmegaflash and execute are not used
                ;for better performance.
                lda idreg
                eor idreg       ;Acc = $ff if MegaFlash exists
                inc a           ;Acc = $00 if MegaFlash exists 
                beq usefpu      ;Branch if MegaFlash exists                
userom:         jmp swrts       ;MegaFlash not exist or FPU Disabled
                                ;Return to main bank and then rts 
                                ;to use ROM implementation

usefpu:         ;
                ;FPU Implementation
                ;
                
                ;Reset Buffer pointers
                stz cmdreg    
                
                ;Send fac and arg to parameter buffer
                ldx #5          ;6 bytes for fac and arg
:               lda fac,x
                sta paramreg
                lda arg,x
                sta paramreg
                dex
                bpl :-
                
                ;Send fac extension
                lda facext
                sta paramreg

                ;Send and Execute the command
                sty cmdreg
                
                ;Pop 2 return addresses by adding 4 to SP
                ;so that we don't return to original ROM code implementation.
                ;Note: These instructions are placed here for two reasons.
                ;1) Do something useful while waiting the calculation result
                ;2) There are two writes to MegaFlash registers back to back.
                ;   sta paramreg
                ;   sty cmdreg
                ;It is possible that Pico cannot keep up with our speed and cannot
                ;set the busy flag in time. Although test program shows that there is
                ;no issue currently, adding delay before polling the busy flag can avoid
                ;potential problems in the future.
                tsx     ;2 Cycles
                inx     ;2 Cycles
                inx     ;2 Cycles
                inx     ;2 Cycles
                inx     ;2 Cycles
                txs     ;2 Cycles
                
                ;Special Handling if command is CMD_FOUT
                cpy #CMD_FOUT
                beq fout_result

                ;Wait until operation completes
                ldx #5          ;Preload x=5, routine at noerr assumes x=5                
:               bit statusreg
                bmi :-          
                
                ;Test Error Flags
                lda paramreg
                beq noerr       ;Bypass all tests if errorcode = 0
                asl a           ;C=bit 7, N=bit 6
                bcs jmpov       ;Test bit 7, Branch if Overflow Error
                bmi jmp0div     ;Test bit 6, Branch if Divison by Zero Error
                bne jmpiqerr    ;Test bit 5-0, Branch to Illeqal Quantity Error
                                ;if any of these bit is set

                ;Get Calculation Result
noerr:          ;x=5   
:               lda paramreg    ;Get FAC
                sta fac,x       ;
                dex
                bpl :-
                lda paramreg    ;Get FAC Extension
                sta facext
                jmp swrts       ;Done!
                
                ;
                ;Error Handlers
                ;Those Applesoft Error Handlers are in Main ROM Bank
                ;We can't jmp to them directly.
                ;The solution is to push the destination address to stack
                ;and execute jmp swrts to switch bank + RTS.
                ;
jmpov:          lda #.HIBYTE(overflow-1)      ;Overflow Error                     
                ldx #.LOBYTE(overflow-1)
pushrts:        pha
                phx
                jmp swrts
jmp0div:        lda #.HIBYTE(zerodiv-1)       ;Divison by Zero Error                        
                ldx #.LOBYTE(zerodiv-1)
                bra pushrts
jmpiqerr:       lda #.HIBYTE(iqerr-1)         ;Illegal Quantity Error                           
                ldx #.LOBYTE(iqerr-1)
                bra pushrts
                
fout_result:    ;
                ;Special handling for CMD_FOUT
                ;
                ; The original value of Y register is pushed to stack before calling fpu_exec
                ; and we have increased SP by 4. So, the stack looks like this
                ;
                ; Offset -3: return address of jsr fpu_exec low byte
                ;        -2: return address of jsr fpu_exec high byte
                ;        -1: yval
                ;    SP -->: return address of jsr fpu_xxxx low byte
                ;        +1: return address of jsr fpu_xxxx high byte  
                ;        +2: return address of the caller of FPU routine low byte
                ;        +3: return address of the caller of FPU routine high byte               
                ;
                ; We need to retrieve the original value of Y register from stack
                ; and adjust the stack pointer.
                
                ;Retrieve yval from stack to X and adjust the stack pointer
                tsx
                lda a:$100-1,x          ;Get yval
                tax                     ;Copy to X
                pla                     ;Pop dummy value. To increase SP by 1
                
                ;Wait until operation completes
:               bit statusreg
                bmi :- 
                
                ;Copy the string including NULL characters
                ;to address (stack-1)+yval        
                ldy paramreg    ;Number of Chars (excluding the NULL char)
                beq invalidlen  ;If y=0 or y>20, something's got wrong. Dont copy.
                cpy #21         ;Output an empty string instead
                bge invalidlen  ;            
                
:               lda paramreg
                sta a:stack-1,x ;Force absolute adressing mode
                inx
                dey
                bne :-
invalidlen:     stz a:stack-1,x         ;NULL Terminate the string
                lda #.LOBYTE(stack)     ;Original Implementation sets AY to stack              
                ldy #.HIBYTE(stack)     ;before return                
                jmp swrts
;----------------------------------------------------     
;FADD
;
;FADD is not enabled because test result shows that
;the FPU implementation is actually slower than the 
;original routine even at 1MHz
; 
.if 0     
                .segment "B0_E7C6"
                ;The original code is 
                ;E7C6: LDX $AC
                ;E7C8: STX $92
                jsr fadd
                nop            ;filler byte
                
                .segment "SLOTROM"
fadd:           ldy #CMD_FADD
                jsr fpu_exec
                ldx $ac         ;execute the original code
                stx $92         ;
                rts             ;continue the original implementation
.endif

;----------------------------------------------------
;FMUL FAC = ARG * FAC
;
                .segment "B0_E987"
                ;The original code is
                ;E987: JSR $EAE0
                jsr fmul
                
                .segment "SLOTROM"
fmul:           ldy #CMD_FMUL
                jsr fpu_exec
                jmp $ea0e       ;execute the original code, jmp = jsr + rts
                ;rts            ;continue the original implementation

;----------------------------------------------------
;FDIV
;             
                .segment "B0_EA6B"
                ;The original code is
                ;EA6B: JSR $EB72
                jsr fdiv
      
                .segment "SLOTROM"
fdiv:           ldy #CMD_FDIV
                jsr fpu_exec
                jmp $eb72       ;execute the original code, jmp = jsr + rts
                ;rts            ;continue the original implementation
                


;----------------------------------------------------
;FSIN
;         
                .segment "B0_EFF1"
                ;The original code is
                ;EFF1: JSR $EB63
                jsr fsin
      
                .segment "SLOTROM"
fsin:           ldy #CMD_FSIN
                jsr fpu_exec
                jmp $eb63       ;execute the original code, jmp = jsr+rts
                ;rts            ;continue the original implementation
      
;----------------------------------------------------
;FCOS
;         
;The ROM implementation translates cos to sin.
;ie. cos(x) = sin(x+pi/2)
;If memory space in SLOTROM area is running out,
;remove this function. cos() is still accelerated
;indirectly.
                .segment "B0_EFEA"
                ;The original code is
                ;EFEA: LDA #$66
                ;EFEC: LDY #$F0
                jsr fcos
                nop             ;filler byte
      
                .segment "SLOTROM"
fcos:           ldy #CMD_FCOS
                jsr fpu_exec
                lda #$66        ;execute the original code
                ldy #$f0        ;
                rts             ;continue the original implementation
      
;----------------------------------------------------
;FTAN
;         
                .segment "B0_F03A"
                ;The original code is
                ;F03A: JSR $EB21
                jsr ftan

                .segment "SLOTROM"
ftan:           ldy #CMD_FTAN
                jsr fpu_exec
                jmp $eb21       ;execute the original code, jmp = jsr+rts
                ;rts            ;continue the original implementation


;----------------------------------------------------
;FATN
;    
                .segment "B0_F09E"
                ;The original code is
                ;F09E: LDA $A2
                ;F0A0: PHA
                ;F0A1: BPL $F0A6
                jsr fatn
                
                .segment "SLOTROM"
                ;There is a PHA instruction in the original code.
                ;We can't use RTS to return to the original code.
                ;The solution is to pop the return address from stack
                ;And use JMP to return to the original code
fatn:           ldy #CMD_FATN
                jsr fpu_exec
                pla             ;pop the return address    
                pla             ;
                lda $a2         ;execute the original code
                pha             ;
                jmp $f0a1       ;continue the original implementation

;----------------------------------------------------
;FEXP
;  
                .segment "B0_EF09"
                ;The original code is
                ;EF09: LDA #$DB
                ;EF0B: LDY #$EE
                jsr fexp
                nop             ;filler byte
                
                .segment "SLOTROM"
fexp:           ldy #CMD_FEXP
                jsr fpu_exec
                lda #$db        ;execute the original code, jmp = jsr+rts
                ldy #$ee        ;     
                rts             ;continue the original implementation         

;----------------------------------------------------
;FLOG
;         
;LOG function starts at $E941. But the first section of
;the code is to check if FAC<=0.
;It is more efficient to handle it by 6502 code.So,
;we branch out at $E94B.
                .segment "B0_E94B"
                ;The original code is
                ;E94B: LDA $9D
                ;E94D: SBC #$7F
                jsr flog
                nop             ;filler byte

                .segment "SLOTROM"
flog:           ldy #CMD_FLOG
                jsr fpu_exec
                lda $9d         ;execute the original code
                sbc #$7f        ;
                rts             ;continue the original implementation



;----------------------------------------------------
;FSQR
;     
                .segment "B0_EE8D"
                ;The original code is
                ;EE8D: JSR $EB63
                jsr fsqr
                
                .segment "SLOTROM"
fsqr:           ldy #CMD_FSQR
                jsr fpu_exec
                jmp $eb63       ;execute the original code, jmp = jsr+rts
                ;rts            ;continue the original implementation


;----------------------------------------------------
;FOUT - Format a number as a string
;
; Input: Y (can be 0 or 1)
;
; Convert the FAC to string.
; Store the result at address (stack-1)+y with a NULL character
; at the end. Finally set YA to stack ($100) before return
;
                .segment "B0_ED36"
                ;The original code is
                ;ED36: LDA #$2D
                ;ED38: DEY
                jsr fout

                .segment "SLOTROM"
fout:           ;Use Original Implementation if fac=0. It's much faster.
                lda fac
                beq foutorg
                
                phy             ;Save y-register, fpu_exec routine also needs it.     
                ldy #CMD_FOUT
                jsr fpu_exec
                ply             ;restore y-register

foutorg:        lda #$2d        ;execute the original code
                dey             ;
                rts             ;continue the original implementation





.endif  ;FPUSUPPORT