                .include        "apple2.inc"

                ;
                ; Constants Definitions
                ; The .inc file is shared with Firmware Project
                ;                
                .include "../common/defines.inc"

                .code
                .importzp sreg,sp
                .importzp tmp1,tmp2,tmp3,tmp4
                .importzp ptr1,ptr2,ptr3,ptr4
                .import popa

                .export _ResetCPU,_IsAppleIIcplus
                .export _ToUppercase,_ZeroMemory
                .export _HasFPUSupport,_ReadOpenAppleButton,_Delay


;
; ROM Entry Point
;
appleii         :=      $FB60   ;Clear screen and show Apple IIc
        
;/////////////////////////////////////////////////////////
; void __fastcall__ ResetCPU()
; Soft Reset the machine immediately
;
_ResetCPU:      bit $c082       ;Switch in ROM
                jsr appleii     ;Show Apple II on screen to give immediate feedback to user
                sta $3f3        ;Destory the PWRUP bytes
                sta $3f4        ;
                jmp ($fffc)     ;Reset!



;/////////////////////////////////////////////////////////
; uint8_t __fastcall__ IsAppleIIcplus()
; return non-zero if running on IIc+ (ROM5)
; return zero if not ROM5
;
_IsAppleIIcplus:
                ldx #0          ;Preload X=0
                bit $c082       ;Switch in ROM
                lda $fbbf       ;$fbbf = 05 for IIc+
                bit $c080       ;Restore to LC bank 2
                cmp #$05
                beq @iicplus    ;If equal, return a=5, x=0
                txa             ;else return a=0, x=0
@iicplus:       rts     


;/////////////////////////////////////////////////////////
; void __fastcall__ ToUppercase(char* s)
; Convert a string to uppercase
; Lenght of string <256
;                
_ToUppercase:
                sta ptr4
                stx ptr4+1

                ldy #0
@loop:          lda (ptr4),y
                beq @exit       ;NULL character?
                cmp #'a'
                bcc :+
                cmp #'z'+1
                bcs :+
                and #%11011111
                sta (ptr4),y
:               iny
                bne @loop       ;bne instead of bra to avoid dead-loop
@exit:          rts	

;/////////////////////////////////////////////////////////
; void __fastcall__ ZeroMemory(uint8_t len,void* dest)
; Clear memory region to zero.
; Length is limited to 255 bytes only
;
; Input: len  - number of bytes
;        dest - pointer to destination
;
_ZeroMemory:
                ;write dest pointer, self-modifying code
                sta @loop+1
                stx @loop+2
                
                ;Get len from stack
                jsr popa
                tax             ;Update z-flag
                beq @exit       ;len=0?
                sta @cpxinst+1  ;Self modifying code
                
                ldx #0
@loop:          stz $ffff,x     ;dest,x
                inx
@cpxinst:       cpx #$ff        ;#len
                bne @loop
@exit:          rts
                
                
;/////////////////////////////////////////////////////////           
; bool __fastcall__ HasFPUSupport();
; Check if the firmware has FPU support enabled
;
; Output: bool - Apple Firmware has FPU support enabled
;
; $C7FF = FPU_SUPPORT_SIGNATURE if FPU Support is enabled
; in the firmware
_HasFPUSupport:
.ifndef TESTBUILD                
                ldx #0          ;Preload X=0, A=0 (false)
                txa             ;
                ldy $C7FF
                cpy #FPU_SUPPORT_SIGNATURE
                bne @nofpu
                inc a           ;X=0, A=1 (true)        
@nofpu:         rts
.else
                ldx #0
                lda #1
                rts
.endif     


;/////////////////////////////////////////////////////////     
; bool __fastcall__ ReadOpenAppleButton()
; Read Open-Apple Key status 
;
; Output: bool - status of Open Apple key
;
_ReadOpenAppleButton:
                lda #0          ;Preload a=0, x=0 (false)
                tax 
                bit $C061       ;Joystick Button 0
                bpl @oa_off
                inc a           ;a=1, x=0 (true)
@oa_off:        rts

;/////////////////////////////////////////////////////////     
; void __fastcall__ Delay(uint8_t n)
; To call the system wait routine. 
;
; Input: n - delay duration of wait routine
;
_Delay:
                bit $c082       ;Switch in ROM
                jsr $fca8       ;Monitor Wait routine
                bit $c080       ;Restore to LC bank 2
                rts