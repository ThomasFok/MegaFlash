;--------------------------------------------------------
; Apple IIc MegaFlash Firmware
; Module: Slot Rom and Bootcode
;

                ;All codes default to this segment
                .define HOMESEGMENT "ROM3"

                .setcpu "65C02"
                .segment HOMESEGMENT
                .reloc

                .include "buildflags.inc"
                .include "defines.inc"             
                .include "macros.inc"


                ;
                ; Imports
                ;
                .import getdevstatus,clockdriver
                .importzp aval,xval,yval

                ;
                ; Exports
                ;
                .export numbanks,pwrup,fpuenabled,toshowbootmenu        
                .export scratch4,scratch5,scratch6,scratch7
                .export copybc
                .exportzp SLOTX16                       


;
; Slot Configuration
;
SLOT            EQU     4               ;IIc/IIc+

SLOTX16         EQU     (SLOT*16)       ;$n0
SLOTCN          EQU     ($C0+SLOT)      ;$Cn


;
;Slot Scratch RAM
;
numbanks        :=      ($0478 + SLOT)  ;Num of Slinky RAM bank in Apple IIc
pwrup           :=      ($04F8 + SLOT)  ;Set to $A5 after power up by Slinky Firmware
toshowbootmenu  :=      ($0578 + SLOT)  
fpuenabled      :=      ($05F8 + SLOT)  ;MSB is set if FPU is enabled in Control Panel
scratch4        :=      ($0678 + SLOT)  
scratch5        :=      ($06F8 + SLOT)
scratch6        :=      ($0778 + SLOT)
scratch7        :=      ($07F8 + SLOT)





;****************************************************************
;  #####                          ######   #######  #     # 
; #     #  #        ####   #####  #     #  #     #  ##   ## 
; #        #       #    #    #    #     #  #     #  # # # # 
;  #####   #       #    #    #    ######   #     #  #  #  # 
;       #  #       #    #    #    #   #    #     #  #     # 
; #     #  #       #    #    #    #    #   #     #  #     # 
;  #####   ######   ####     #    #     #  #######  #     # 
;****************************************************************                                                           

;---------------------------------------------------
; $Cn00-CnFF Slot ROM

                .segment "SLOTROM_00"   ; At Offset 0 i.e. $Cn00
                .org (SLOTCN*$0100)
                cmp #$20                ;Signature Bytes
                cmp #$00
                cmp #$03
                
                cmp #$00                ;Smartport Signature Byte. =$3C if no smartport

boot:           lda #MODE_COPYBC        ;Copy bootcode to RAM
                jsr slxeq               ;
                ;A=unitCount, X=prodosPresent
                
                jmp BCRUN               ;execute the bootcode
                
                ;******************************************
                ;Device Signature Byte #1
                ;******************************************
                .assert .LOBYTE(*) = $10, error, "Signature not at $Cn10"
                .byte SIGNATURE_AT_10

                ;***********************************************
                ;$90 and $4B must be in location $C411 and $C412
                ;$4B duplicate the mouse entry as found in slot.
                ;This fix enables some program to work correctly.
                ;***********************************************
                .assert .LOBYTE(*) = $11, error, "Code not at $Cn11"
                .byte $90,$4B           

;---------------------------------------------------------------
; ProDOS and Smartpoint Entry Point
;
;
;ProDOS Block Device Driver Entry Point
p8entry:        lda #MODE_PRODOS
                skip2           ;skip lda instruction
                        
;Smartpoint Driver Entry Point
spentry:        lda #MODE_SP
;--------             
                ;Common entry point of ProDOS and Smartport Driver
                        
                ;Save Stack Pointer to X
                tsx                     
                
                ;Execute the command, A=mode, X=Stack Pointer
                jsr slxeq
                
                ;setup carry flag
                cmp #1  ;Carry =1 if a is not 0 (i.e. >=1)
                rts                     
                        
;--------------------------------------------
; Speed Test routine to measure the performance
; Read Block 0 to $2000 and time required is printed
; to screen.
; (For Development only)
.if 0
                .include "../common/defines.inc"   
speedtest:      ldx #STCMDLEN-1         
:               lda stcmd,x             
                sta pdCommandCode,x
                dex
                bpl :-
                
                lda #CMD_RESETTIMER_US        ;Start Timer command
                sta cmdreg
                
                ;execute
                jsr p8entry       ;p8 entry
                lda #CMD_GETTIMER_US          ;stop Timer
                sta cmdreg
                ;no need to delay since running at 1MHz
                
                ;read the time elapsed and print
                lda paramreg    ;low byte              
                pha
                lda paramreg    ;high byte
                jsr prbyte      ;Print Acc as hex  
                pla
                jmp prbyte      ;jsr + rts
                
stcmd:          .byte PD_READ   ;read command
                .byte $40       ;UnitNum (e.g. $40)
                .addr $2000     ;Load to address $2000
                .word 0000      ;Block Number
STCMDLEN        = (* - stcmd)    
.endif                        

;
; Data Table for MegaFlash Device
;                        
                .segment "SLOTROM_F8" ;at $CnF8
                .org (SLOTCN*$0100+$F8)

                ;
                ;Clock Driver Offset at $CnF8
                ;
                .assert clockdriver=0 || .HIBYTE(clockdriver)=SLOTCN, error, "clock driver not in slot rom"
                .byte   .LOBYTE(clockdriver)    ;Clock Driver Entry Point, =0 if clock driver is not implemented         

                ;
                ;Firmware Version at $CnF9
                ;
                .byte   FWVERSION               ;Our Firmware Version

                ;
                ;Device Signature Byte #2 at $CnFA
                ;
                .assert .LOBYTE(*) = $FA, error, "Signature not at $CnFA"
                .byte SIGNATURE_AT_FA
                
;
; Data Table for ProDOS/Smartport Interface
;
                
;$CnFB
;Smartport ID Type
; bit 7         Extended Smartport
; bit 6-2       Reserved
; bit 1         SCSI
; bit 0         RAM Card
                
;$CnFC/FD       
; Disk Capacity in number of blocks. =0 to use Status command to get the value.

; $CnFE = status bits (BAP p7-14)
;  7 = medium is removable
;  6 = device is interruptable
;  5-4 = number of volumes (0..3 means 1..4)
;  3 = device supports Format call
;  2 = device can be written to
;  1 = device can be read from (must be 1)
;  0 = device status can be read (must be 1)            

                
                .byte   $00                     ;Smartport ID Type byte, 01=RAM Drive, 0= HDD
                .word   0                       ;Block Size, 0=Retrieve from Status command
                .byte   %01010111               ;Bit 3 must be 0 for Apple Sys Util to be able to format the drive
                .byte   .LOBYTE(p8entry)        ;ProDOS and Smartpoint Entry point
                                

;***********************************************************************
; ######                               #####                          
; #     #   ####    ####   #####      #     #   ####   #####   ###### 
; #     #  #    #  #    #    #        #        #    #  #    #  #      
; ######   #    #  #    #    #        #        #    #  #    #  #####  
; #     #  #    #  #    #    #        #        #    #  #    #  #      
; #     #  #    #  #    #    #        #     #  #    #  #    #  #      
; ######    ####    ####     #         #####    ####   #####   ###### 
;***********************************************************************                                                                     

;---------------------------------------------------------------
; The boot code is stored in ROM at address bcloc. The code is copied
; to RAM address BCRUN by copybc routine before execution. It is necessary 
; because the boot code uses lots of System ROM routines and 
; the code cannot be executed from Aux ROM bank. copybc routine also setup
; A and X registers to indicate unit count and existance of ProDOS BASIC.SYSTEM



                .segment HOMESEGMENT
                .reloc
bcloc:                                  ;Physical Address of bootcode in ROM
                
                .org BCRUN              ;Switch to absolute code
bcstart:        sei                     ;Disable Interrupt During boot

                ;Store A and X to memory
                ;A = unit Count, X=ProDOS present
                sta unitCount
                stx prodosPresent

                lda kswh
                pha
                ;Reset i/o hook
                jsr setkbd
                jsr setvid
                pla
                cmp #SLOTCN             ;IN#n ?
                bne rdblk0              ;Branch if not

                .if BOOTANY
                jsr askdrv              ;Ask user which drive to boot from
                .else
                lda #$80
                tsb btunitnum           ;Set MSB to enable boot from drive 2
                .endif

                ;Read Block 0 to $800 from unit $n0
rdblk0:         ldx #BTCMDLEN-1         
:               lda btcmd,x             
                sta pdCommandCode,x
                dex
                bpl :-
                .assert BTCMDLEN-1<=$80, error, "bpl range error"
                
                jsr p8entry     ;Load Block 0 by ProDOS driver
                bcs bootfail
        
                ldx $800        ;check if first byte of bootloader is 1
                dex             ;
                bne bootfail
                ldx $801        ;check if second byte is not 0
                beq bootfail    ;
                inx             ;check if second byte is not $ff
                beq bootfail    ;
                ;Note: The stock firmware only check $800=1 and $801!=0.
                ;Since Flash memory is used, the flash content is all $FF
                ;if it is empty. Also, $FF is not a valid 65C02 op codes
                ;So, we check $801!=$FF as well. 
              
                ;Execute the bootloader!
                ldx btunitnum   ;X should contain unit number when jmp to bootloader
                jmp $801        ;Execute bootloader

bootfail:       ;($00) = $Cn00 if we are in auto-boot (power up/forced cold start)
                lda  $00
                bne  notautobt
                lda  $01
                cmp  #SLOTCN
                bne  notautobt
                
                ;IIc/IIc+: Try next boot slot
                lda #NEXTBOOTSLOT
                sta $01
                jmp ($00)

prnbootfail:                        
notautobt:      ;The boot code must have been called manually (e.g. pr#4)
                ;Printe Error and goto Applesoft
                jsr vtab23              ;Move Cursor to bottom
                lda #.LOBYTE(btfailmsg)
                ldx #.HIBYTE(btfailmsg)
                jsr printmsg
tobasic:        jmp applesoft


                .if BOOTANY
                
                ;Try to reconnect ProDOS BASIC.SYSTEM
tryprodos:      lda prodosPresent       ;!=0 if ProDOS BASIC.SYSTEM is in memory                
                beq tobasic             ;ProDOS not found
                jmp $3d0                ;Try to warm start ProDOS               

;---------------------------------------
;Ask user which drive to boot from
;
;                                
askdrv:         ;Get Number of Units
                lda unitCount
                beq prnbootfail ;Branch if Unit count is 0
                
                ;Prepare prompt message
                clc
                adc #$b0        ;'0'
                ;A=ASCII of last drive number
                sta drvnumaddr  ;Self-Modifying Code
                
                inc a
                sta cmpkey+1    ;Self-Modifying Code, = ASCII of last drive number + 1 
                
askagian:       stz ch          ;Print from left-most position
                lda #.LOBYTE(askdrvmsg)
                ldx #.HIBYTE(askdrvmsg)
                jsr printmsg
          
                ;wait for input
                lda #$a0        ;space char
                jsr keyin
                pha
                jsr cout1       ;echo the result
                jsr cr          ;newline
                pla
          
                ;Validate key pressed
                ;Key 1 to N or Ctrl-C
                cmp #$83        ;Ctrl-C, exit to basic
                beq tryprodos   ;
                cmp #$b1        ;'1'
                blt askagian    ;Invalid Input, try again
cmpkey:         cmp #$FF        ;Note Self-Modifying Code above
                bge askagian    ;Invalid Input, try again
                
                ;Convert Key Code to ProDOS 2.5 UnitNum
                sec
                sbc #$b1                ;A = 0-7
                lsr                     ;Shift LSB to Carry
                bcc :+
                ora #$80                ;set MSB
:               ora #SLOT*16            ;set SLOT bits
                sta btunitnum
                .endif  ;BOOTANY
printmsgrts:    rts
                
;-------                
printmsg:       ;address in ax, replace '@' with value in Y
                sta loadchr+1           ;Self-Modifying Code, low byte of address of message
                stx loadchr+2           ;Self-Modifying Code, high byte of address of message

                ldx #0
loadchr:        lda $FFFF,x             ;Note Self-Modifying Code above    
                beq printmsgrts         ;Branch to nearest rts
                jsr cout1
                inx
                bra loadchr
;---------------------------------------                

btcmd:          .byte PD_READ   ;read
btunitnum:      .byte SLOTX16   ;Default UnitNum (e.g. $40)
                .addr $800      ;Load to address $800
                .word 0         ;Block Number
BTCMDLEN        = (* - btcmd)

btfailmsg:      hascii "Unable to boot from MegaFlash"
                .byte   $87,$00     ;Bell and null-terminated 
askdrvmsg:      hasciiz "Boot from which drive (1-@,CTRL-C)?"
drvnumaddr      = askdrvmsg+25   ;address to @ character in askdrvmsg string
;-------------------------
;Memory Variables
unitCount:      .res 1
prodosPresent:  .res 1

                .assert (*-bcstart) <=256, error, "Boot Code >256 bytes"
BCLEN           = .LOBYTE(*-bcstart)    ;Length of bootcode
                

;--------------------------------------------------------
;Copy bootcode to RAM location BCRUN
;Under normal operation, bootcode at bcloc is copied.
;If SHOWBOOTMENUMAGIC is set, it jumps to loadbootmenu
;routine. The routine will copy boot menu code to BCRUN instead
;
;Copy whole page (256 bytes) regardless the acutal size
;to reduce code length
;
;The boot code is limited to 256 bytes. So, this routine
;does some work for the boot code
;
;1) It calls getdevstatus driver routine and save number
;   of Units to aval
;
;2) It tries to detect if ProDOS is present. Set xval to
;   non-zero if ProDOS is detected.
;
;If ProDOS BASIC.SYSTEM is present, the following conditions are tested
; $03D0: JMP $BExx
; $BE00: JMP $xxxx
; $BF00: JMP $xxxx
; $BFB0 != $A0
;  
; After Ctrl-OA-Reset, the firmware writes $A0 to $BFB0 to
; trash memory. In this case, ProDOS is corrupted.

                .segment HOMESEGMENT
                .reloc
copybc:         
                ;Copy boot code to RAM
                ldx #0
:               lda bcloc,x
                sta BCRUN,x
                inx
                bne :-
                
                ;Call getdevstatus and store Unit Count to aval
                jsr getdevstatus
                sta aval
                
                .if BOOTANY
                ;Is ProDOS present? 
                stz xval        ;Assume no ProDOS 
                
                lda #$4c        ;opcode of jmp
                cmp $3d0
                bne noprodos
                cmp $be00
                bne noprodos
                cmp $bf00
                bne noprodos
                
                lda $3d2	
                cmp #$be
                bne noprodos
                
                lda $BFB0
                cmp #$A0	
                beq noprodos	

                inc xval        ;ProDOS found, change xval to 1
                .endif          ;BOOTANY
                
noprodos:       rts
                
                

