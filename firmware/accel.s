;--------------------------------------------------------
; Apple IIc MegaFlash Firmware
; Module: Apple IIc Plus Accelerator Control Code
;
; Based on the source code from
; https://gist.github.com/mgcaret/022bd0bb3ee71f28429972523556416e
;

; Undocumented features/functions/bugs/errors:
; * undocumented command $00 - executed during reset, this inits accelerator and checks for esc key
;   if you issue this command and press esc during the paus, accel switches to standard
;   speed
; * bug: trashes location $0000
; * bug: accelerator control word high byte is never used to config accelerator
; * bug: the reset code appears to want to restore saved state of accelerator but only does so for the slots
; * bug: write accelerator command trashes the saved accelerator control word
; * bug: some nonfunctional code in set accelerator routine
; above bugs are fixed if fixbugs is nonzero (see below)
; * docs error: accelerator control word low byte docs say:
;    1 = fast, reality: 1 = slow
;    bit 7 = speaker speed, bit 0-6 = slot speed: reality:  speaker at bit 0, slots at 1-7.
; * Uses MIG RAM page 2 as scratchpad and state storage (MIG RAM is 32-byte window at $CE00)
;   referred to in the rest of this file as mig[offset(s)]
;   The front side to the MIG RAM (not connected to the IWM) is controlled by access to $CEA0 and $CE20
;   $CEA0 resets the window to page 0.  $CE20 increments the page.  Note the accesses to these locations
;   near the beginning of the $FD00 code.
;   The MIG is visible when the altnernate firmware is visible, its address space is $CE00-$CFFF
;   But only $CE00-$CE1F view the RAM, everything else looks like floating bus.
; Missing features present in the hardware but unused:
; * Accelerator has clock scaler, code always sets 4.00 MHz

; uses ZP from $00 to $03 after saving it to mig[$10-$13]
; $00: original stack pointer at entry
; $01: command number
; $02-$03: user buffer pointer (if command needs it)

; mig memory (at $CE00) usage:
; $00: powerup byte 1 - $33 after initialization 
; $01: powerup byte 2 - $55 after initialization
; $02: current accelerator control word - low byte - speeds (1 = slow)
;     b7-b1: port (slot) 7-1
;     b0: speaker
;     init byte is $67 (01100111 - speaker, ports 1, 2, 5, 6 = slow)
; $03: current accelerator control word - high byte - misc
;     b7: reserved
;     b6: paddle speed (1 = slow)
;     b5: reserved
;     b4*: register lock/unlock state (1 = locked, undocumented)
;     b3: accelerator enable (1 = disabled) - Not set by write accelerator, see docs.
;     b2-b1: reserved
;     b0: reserved but in initial value for ACW high byte
;     init byte appears to supposed to be $51 (01010001), but never set
; $04: keyboard key value to check for <ESC> at reset
; $10-$13: saved ZP values

; Accelerator registers - Appear to follow ZIP Chip without any customization.
; the ZIP chip 2.0 software has no problems detecting and configuring the IIc Plus
; accelerator
; $c05a - Write $A5 lock
;         Write $5a * 4 to unlock
;         Write anything else to slow down to 1 MHz indefinitely
; $c05b - Write anything here to enable accelerator
;         Read status
; $c05c - R/W Slot/speaker speed
; $c05d - Write system speed
; $c05e - Write I/O delay bit 7 (1 disable)
;       - Read softswitches
; $c05f - Paddle speed/language card cache

; ----------------------------------------------------------------------------
; ca65 setup
        .psc02
; ----------------------------------------------------------------------------
; conditions


; equates
PAGE0   = $00
CALLSP  = PAGE0+0
COUNTER = CALLSP        ;Shared
COMMAND = PAGE0+1
UBFPTRL = PAGE0+2
UBFPTRH = PAGE0+3
ZPSIZE  = 4

; misc
STACK   = $0100
SWRTS2  = $C784
; I/O page
IOPAGE  = $C000
KBD     = IOPAGE+$00
KBDSTR  = IOPAGE+$10
ZIP5A   = IOPAGE+$5A
ZIP5B   = IOPAGE+$5B
ZIP5C   = IOPAGE+$5C
ZIP5D   = IOPAGE+$5D
ZIP5E   = IOPAGE+$5E
ZIP5F   = IOPAGE+$5F
; MIG
MIGBASE = $CE00
MIGRAM  = MIGBASE
PWRUPB0 = MIGRAM+0
PWRUPB1 = MIGRAM+1
ACWL    = MIGRAM+2
ACWH    = MIGRAM+3
KBDSAVE = MIGRAM+4
CFGBYTE1= MIGRAM+5      ;MegaFlash Config Byte 1
CFGBYTE2= MIGRAM+6      ;MegaFlash Config Byte 2
PWRUPSET = MIGRAM+7   ;=1 if Power Up speed is set
ZPSAVE  = MIGRAM+$10

MIGPAG0 = MIGBASE+$A0
MIGPAGI = MIGBASE+$20
; fixed values
PWRUPV0 = $33
PWRUPV1 = $55
ESCKEY  = $9B


; ----------------------------------------------------------------------------
; Aux bank delay, called by accelerator init routine, presumably to give
; time for <ESC> key to be recognized.  This is also what messes up the beep
; see http://quinndunki.com/blondihacks/?p=2471
; input: A = delay counter
ADELAY  := $FCB5

; ----------------------------------------------------------------------------
; main body of code, you arrive here by calling jsr $C7C7 while the main bank
; is active.  The system switches banks and jumps to $FD00.
        .segment "ACCEL"
        .export togglecpuspeed,showcpuspeed,setpowerupspeed
        .export showcpuspeedswrts,toggleshowcpuspeedswrts
        
        .org $FD00
        
        .proc ACCEL
        php                                     ; save processr status
        sei                                     ; disable interrupts
        phy                                     ; save y reg 
        phx                                     ; save x reg
        bit     MIGPAG0                         ; set MIG page 0
        bit     MIGPAGI                         ; MIG page 1
        bit     MIGPAGI                         ; MIG page 2
        
        ; Next routine reads $0000-$0003, stores into $CE10-$CE13, and zeros them
        ldx     #ZPSIZE-1                       
@loop:  lda     PAGE0,x                         ; get ZP location
        sta     ZPSAVE,x                        ; Save to MIG
        stz     PAGE0,x                         ; Zero ZP location
        dex                                     ; next
        bpl     @loop                           
        ; The following routine gets the command and parameters off of the stack
        ; Next combination of instructions puts the stack pointer into all 3
        ; registers, then increments it in y.
        tsx                                     ; get sp
        txa                                     ; copy it...
        tay                                     ; to y
        iny                                     ; now x is sp, y is sp+1
        lda     STACK+6,x                       ; reach into stack for command, 6-byte offset for return
                                                ; address and saved registers
        sta     COMMAND                         ; put into ZP $02
        cmp     #$05                            ; $05 = Read Accelerator - first command w/buffer pointer
        stx     CALLSP                          ; original sp into $01
        bcc     noparm                          ; no buffer pointer to get
        lda     STACK+7,x                       ; buffer pointer low byte
        sta     UBFPTRL                         ; into ZP $03
        lda     STACK+8,x                       ; buffer pointer high byte
        sta     UBFPTRH                         ; into $04
        iny                                     ; Fix index registers for parameters...
        iny                                     ; 
        inx                                     ; 
        inx                                     ; 
noparm: inx                                     ; we go here if it's a no-param call
        txs                                     ; stack now adjusted                  .
        ldx     CALLSP                          ; original SP                    ..
        
        ;CALLSP is not used after this point
        ;So, CALLSP and COUNTER can share the same address
        
        lda     #$05                            ; 5 bytes
        sta     COUNTER                         ; Setup loop counter
@loop:  lda     STACK+5,x                       ; shift stack up
        sta     STACK+5,y                       ; to remove call parameters
        dex                                     ; next from
        dey                                     ; next to
        dec     COUNTER                         ; loop counter decrement
        bne     @loop                           ; loop until zero
        
        lda     COMMAND                         ; get command
        cmp     #$07                            ; bad command number?
        bcc     docmd                           ; no, do command
       
        ldy     #$01                            ; y = bad command exit code ($01)
        bra     acceldn                         ; skip command call
docmd:  asl     a                               ; turn call into jump index
        tax                                     ; and move to x
        jsr     dispcmd                         ; call command function
        ldy     #$00                            ; y = no error exit code ($00
acceldn:

        ; Following code attempts to restore the zero page to what it was
        ldx     #ZPSIZE-1
@loop:  lda     ZPSAVE,x                        
        sta     PAGE0,x                         
        dex                                     
        bpl     @loop

        tya                                     ; get exit code to a
        plx                                     ; get saved x
        ply                                     ; y
        plp                                     ; p

        cmp     #$01                            ; Set C if A!=0
noerr:  jmp     SWRTS2                          ; switch banks and RTS
; ----------------------------------------------------------------------------
dispcmd:
        jmp     (cmdtable,x)                    ; dispatch command
        .endproc
; ----------------------------------------------------------------------------
; Init Accelerator (undocumented)
        .proc   AINIT

        ; following code checks for power up bytes in MIG
        lda     #PWRUPV0                        ; a = $33
        ldx     #PWRUPV1                        ; x = $55?        
        cmp     PWRUPB0                         ; $33=mig[0]?
        bne     coldst                          ; nope, do cold start
        cpx     PWRUPB1                         ; $55=mig[1]?
        beq     warmst                          ; yes

coldst: 
        ; set powerup bytes
        sta     PWRUPB0                         ; $33 to mig[0]
        stx     PWRUPB1                         ; $55 to mig[1]
        stz     PWRUPSET                        ; Set it to 0, indicate power up speed is not set.
        
        ;Init Registers
        ldx     #REGINILEN-1                     
rinilp: lda     regini,x  
        sta     ZIP5C,x     
        dex               
        bpl     rinilp                          

        ldx     acwini                          ; initial bytes for accelerator control word
        ldy     acwini+1                        ; 

setacc: jsr     AUNLK                           ; unlock registers
        jsr     AENAB                           ; enable high speed
        jsr     ASETR                           ; set registers 
        jmp     ALOCK                           ; lock registers, jsr+rts

        ; we get straight here if mig[0] has [$33 $55]
warmst: ldx ACWL
        ldy ACWH
        jsr setacc                                
        jmp restorespeed                        ; jsr + rts
        .endproc
; ----------------------------------------------------------------------------
        ; Enable accelerator high speed mode.
        ; assumes registers are already unlocked.
        .proc   AENAB
        lda     #$08                            ; Refer to bit 3 (acclerator status)
        sta     ZIP5B                           ; Standard Zip ngage accelerator
        trb     ACWH                            ; Clear bit 3 in accelerator control word high byte
        rts                                     ; 
        .endproc
; ----------------------------------------------------------------------------
        ; "Indefinite Synchronous Sequence"
        ; turns off high speed mode.
        ; assumes registers are already unlocked.
        .proc   ADISA
        lda     #$08                            ; Can be anything but $A5 or $5A, so refer to bit 3
        sta     ZIP5A                           ; write once
        tsb     ACWH                            ; set bit 3 in accelerator control word high byte
        rts                                     ;                  `
        .endproc
; ----------------------------------------------------------------------------
        ; lock the accelerator registers
        .proc   ALOCK
        lda     #$A5                            ; standard zip lock byte here
        sta     ZIP5A                           ; write once
        lda     #$10                            ; bit 4
        tsb     ACWH                            ; set it in accelerator control word high byte
        rts                                     ; 
        .endproc
; ----------------------------------------------------------------------------
        ; unlock the accelerator registers
        ; this is the standard ZIP unlock sequence and also sets the state save
        ; in mig[3]
        .proc   AUNLK
        lda     #$5A                            ; standard zip unlock byte here
        sta     ZIP5A                           ; write 4 times
        sta     ZIP5A                           ; 
        sta     ZIP5A                           ; 
        sta     ZIP5A                           ; 
        lda     #$10                            ; bit 4
        trb     ACWH                            ; clear it in accelerator control word high byte
        rts                                     ;                    `
        .endproc
; ----------------------------------------------------------------------------
        ; read accelerator
        ; never actually touches the accelerator except to lock or unlock the
        ; registers, the response comes from mig[2..3]
        .proc   AREAD
        lda     ACWL                            ; mig[2]
        sta     (UBFPTRL)                       ; User buffer[0]
        ldy     #$01                            ; next byte of user buffer
        lda     ACWH                            ; mig[3]
        sta     (UBFPTRL),y                     ; User buffer[1]
        rts                                     ; done
        .endproc
; ----------------------------------------------------------------------------
        ; write accelerator
        .proc   AWRIT
        jsr     AUNLK                           ; unlock registers
        jsr     AENAB                           ; enable accelerator       

        ldy     #$01
        lda     (UBFPTRL)                       ; User buffer[0], new ACWL
        tax                                     ; x = new ACWL
        lda     (UBFPTRL),y                     ; User buffer[1], new ACWH
        tay                                     ; y = new ACWH
        jsr     ASETR                           ; set registers
        
        jmp     ALOCK                           ; lock registers + rts
        .endproc
; ----------------------------------------------------------------------------
        ; set accelerator registers x = new ACWL, y = new ACWH
        .proc   ASETR
        stx     ZIP5C                           ; Store slot speeds
        stx     ACWL                            ; It's also the low byte of accel control word
        sty     ACWH                            ; Store y to ACWH
        
        tya
        and     #$40                            ; bit 6 only
        sta     ZIP5F                           ; paddle speed
        
        lda     #$40                            ; Set bit 6 of ZIP5E, which is undocumented        
        sta     ZIP5E                           ; Well, initial value has bit 6 set, orig does this, too
        
        stz     ZIP5D                           ; Set speed register to 4.000 MHz unconditionally
        stz     ZIP5E                           ; Clear bit 6, Enable synchronous sequences unconditionally
        rts                                     ; 
        .endproc
; ----------------------------------------------------------------------------
        ; jump table for command functions
cmdtable:
        .word   AINIT                           ; $00 - Init (undocumented)
        .word   AENAB                           ; $01 - Enable Accelerator
        .word   ADISA                           ; $02 - Disable Accelerator
        .word   ALOCK                           ; $03 - Lock Accelerator
        .word   AUNLK                           ; $04 - Unlock Accelerator
        .word   AREAD                           ; $05 - Read Accelerator
        .word   AWRIT                           ; $06 - Write Accelerator
; ----------------------------------------------------------------------------
acwini: .byte   $67                             ; Accelerator control word init low byte
        .byte   $51                             ; Accelerator control word init high byte

;Registers init value
regini: .byte   $67                             ; $c05c: speaker, slot 1,2,5,6 = slow
        .byte   $00                             ; $c05d: 4 MHz
        .byte   $C0                             ; $c05e: bit 7 (enable I/O synchronous delay)
                                                ;        bit 6 undocumented
        .byte   $00                             ; $c05f: bit 7 (off, enable lang card cache)
                                                ;        bit 6 (off, disable paddle delay)
REGINILEN = 4


; ----------------------------------------------------------------------------
;Display "Normal" on screen
        .proc   NORMAL
        ldy     #$06                            ; 6 characters
@loop:  lda     msg-1,y                         ; Get message byte
        sta     $048F,y                         ; Put on screen
        dey
        bne     @loop
        rts                                     ; done               `
msg:    .byte   $CE, $EF, $F2, $ED, $E1, $EC    ; 'Normal'
        .endproc

; ----------------------------------------------------------------------------
;Display " Fast " on screen
        .proc   FAST
        ldy     #$06                            ; 6 characters
@loop:  lda     msg-1,y                         ; Get message byte
        sta     $048F,y                         ; Put on screen
        dey
        bne     @loop
        rts                                     ; done               `
msg:    .byte   $A0, 'F'|$80, 'a'|$80, 's'|$80, 't'|$80, $A0    ; ' Fast '
        .endproc
        
; ----------------------------------------------------------------------------  
; Toggle CPU speed 
; call from auxbank only
togglecpuspeed:
        jsr     setmigpage2
     
        lda     ACWH
        eor     #$08                            ; Toggle bit 3
        sta     ACWH
        ;fall into restorespeed

; ----------------------------------------------------------------------------  
; Restore speed setting according to ACWH
; Assume MIG Window has been set to page 2
; 
restorespeed:
        jsr     AUNLK                           ; Unlock Accelerator   
        lda     ACWH
        and     #$08                            ; Test bit 3
        beq     setfast
setnormal:        
        ;change to normal speed
        jsr     ADISA                           ; Disable accelerator
        jmp     ALOCK                           ; Lock accelerator + rts
        
setfast: 
        ;change to fast speed
        jsr     AENAB                           ; Enable accelerator
        jmp     ALOCK                           ; Lock accelerator + rts


; ----------------------------------------------------------------------------
; Show CPU Speed Normal or Fast to screen
; Call from auxbank rom bank only
;
showcpuspeed:
        jsr     setmigpage2

        lda     ACWH
        and     #$08                            ; Test bit 3
        beq     FAST
        bra     NORMAL



;----------------------------------------------------------------------------      
;
; For Boot Menu - We have plenty of ROM space here! 
; 
;----------------------------------------------------------------------------   

; -----------------------------------------------
; Toggle CPU Speed + Show Cpu Speed + jmp to SWRTS     
; Call from auxbank rom bank only
;
toggleshowcpuspeedswrts:        
        jsr togglecpuspeed
        ;fall into showcpuspeedswrts


; -----------------------------------------------
; Show CPU Speed then jmp to SWRTS2
; Call from auxbank rom bank only
;
showcpuspeedswrts:
        jsr showcpuspeed
        jmp SWRTS2

;--------------------------------------------------------
; Set Power Up CPU Speed
; This routine can only be called from Aux ROM bank.
;
; Input: a = 0 for fast, !=0 for normal
;
; Note: The Power Up CPU Speed setting is for power up
; only. Ctrl-OA-Reset should keep the speed setting unchanged.
; The main firmware does not provide any way to distingulish
; Power Up and Ctrl-OA-Reset. So, this routine is used.
;
; On Power up, AINIT routine is called. It sets PWRUPSET to 0.
; When this routine is called, it checks this byte. If it is
; 0, the call is accepted. Then, it changes the byte to 1.
; All subsequent changes to CPU speed are rejected.
;
; It also shows Normal on screen if current speed is 1MHz.
;
setpowerupspeed:
        jsr     setmigpage2
        ldx     PWRUPSET
        bne     show_normal     ;If PWRUPSET!=0, don't change CPU speed
        inc     PWRUPSET        ;Set it to 1, refuse all subsequent changes to CPU speed.

        pha                     ; Save A
        jsr     AUNLK           ; Unlock Accelerator
        pla                     ; Restore A and setup Z-flag
        beq     setfast         ; beq = jsr + rts
        jsr     setnormal       

show_normal:        
        ;show Normal if accelerator is disabled
        jsr     setmigpage2

        lda     ACWH
        and     #$08            ; Test bit 3
        beq     rts0
        jmp     NORMAL          ; jmp = jsr + rts, show Normal on screen
        
;----------------------------------------------------------------------------
; Set MIG window to Page 2
setmigpage2:
        bit     MIGPAG0                         ; set MIG page 0
        bit     MIGPAGI                         ; MIG page 1
        bit     MIGPAGI                         ; MIG page 2
rts0:   rts   