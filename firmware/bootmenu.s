;--------------------------------------------------------
; Apple IIc MegaFlash Firmware
; Module: Boot Menu
;

;**************************************************************************
; ######                          #     #                         
; #     #   ####    ####   #####  ##   ##  ######  #    #  #    # 
; #     #  #    #  #    #    #    # # # #  #       ##   #  #    # 
; ######   #    #  #    #    #    #  #  #  #####   # #  #  #    # 
; #     #  #    #  #    #    #    #     #  #       #  # #  #    # 
; #     #  #    #  #    #    #    #     #  #       #   ##  #    # 
; ######    ####    ####     #    #     #  ######  #    #   ####  
;**************************************************************************

                .setcpu "65c02"
                .segment "ROM1"
                .reloc

                .include "buildflags.inc"
                .include "defines.inc"
                .include "../common/defines.inc"
                .include "macros.inc"


                ;
                ;imports
                ;
                ;From slotrom.s
                .import toshowbootmenu

                ;From accel.s (IIC+ Only)
                .ifdef IICP
                .import showcpuspeedswrts,toggleshowcpuspeedswrts
                .endif
                

                ;
                ;exports
                ;
                .export copybm
                
                
;
; Build Configuration/Flags
;

;
; Base Memory Address of Text Line
;
LINE1   = $400
LINE2   = $480
LINE3   = $500
LINE4   = $580
LINE5   = $600
LINE6   = $680
LINE7   = $700
LINE8   = $780
LINE9   = $428
LINE10  = $4A8
LINE11  = $528
LINE12  = $5A8
LINE13  = $628
LINE14  = $6A8
LINE15  = $728
LINE16  = $7A8
LINE17  = $450
LINE18  = $4D0
LINE19  = $550
LINE20  = $5D0
LINE21  = $650
LINE22  = $6D0
LINE23  = $750
LINE24  = $7D0


      

;--------------------------------------------------------------------
; Boot Menu Entry Patch
; To show Boot Menu when Ctrl-CA-Reset is pressed.
;
; The routine from $FAC8 to $FAD6 check the status of OA (Open Apple)
; and CA (Closed Apple) key during reset.
; If only OA is pressed, it jumps to PWRUP ($FAA6) to cold start.
; If both keys are pressed, it jumps to BANGER ($C7C1) to do self-test.
; If none is pressed, it simply RTS.
;
; Patch it so that when only CA is pressed, a Boot Menu is shown.
; When this condition is detected, MSB of screen hole address
; toshowbootmenu is set and then jmp to PWRUP to continue 
; the boot process. The coldstartinit routine in megaflash.s 
; will display the boot menu.

                .segment "B0_FAC8"
                .reloc
                
pwrup           := $FAA6
banger          := $C7C1                


bmentrypatch:   stz toshowbootmenu      ;Assume not display boot menu
                asl butn1               ;C-Flag = CA status
                bit butn0               ;N-Flag = OA status
                jmp bmentrypatch2       ;Continue there
                
                .segment "SLOTROM"
                .reloc
bmentrypatch2:
                bmi oa_on               ;Branch if OA is pressed
                bcc to_rts              ;Branch if CA is not pressed
                dec toshowbootmenu      ;Enable Boot Menu, by change it from 0 to $FF
to_pwrup:       jmp pwrup               ;Then, goto pwrup
to_rts:         rts                     ;Ctrl-Reset
oa_on:          bcc to_pwrup            ;Branch if CA is not pressed
to_banger:      jmp banger              ;Self-Test
                
                
                
;--------------------------------------------------------------------
; Copy boot menu code from bmloc to BMRUN
; Copy 512 bytes regardless the actual size of the code
;               
                .segment "ROM2"
                .reloc               
copybm:         
                ldx #0
:               lda bmloc,x
                sta BMRUN,x
                lda bmloc+$100,x
                sta BMRUN+$100,x
                inx
                bne :-
                rts

;--------------------------------------------------------------------
; Boot Menu Code
; Max Length: 512 Bytes
;
                .segment "ROM2"
                .reloc
bmloc:                                  ;Physical Address of boot menu in ROM
                
                .org BMRUN              ;Switch to absolute code
counter         := $03                  ;Use $03-04 as counter
mfexist         := $05                  ;=0 if megaflash exist


bmstart:        
                jsr appleii             ;Clear Screen and Show Apple IIc
                jsr chkmegaflash
                sta mfexist
;--------------
; Display the menu on screen by writing to screen memory directly
; It also trashes the bytes at $00 and $01. So, when we call
; the boot code at $C400-C600, the code will not jump to next slot
; if it fails to boot.
;
again:    
                .ifdef IICP
                sta rombank             ;the routine is in aux bank                
                jsr showcpuspeedswrts   ;show CPU speed and return to main bank
                .endif
                
                ;Show Clock
                jsr displaytime

                ldx #0
                ;Use $00-$01 as pointer to screen memory
                lda menumsg,x
nextmsg:        sta $01         ;High Byte of addr
                inx
                lda menumsg,x   ;Low Byte of Addr
                sta $00         ;

                ;Copy the message
:               inx             ;Next char
                lda menumsg,x   ;load char
                beq @exit       ;Zero = End of Messge     
                bpl nextmsg     ;MSB clear, it is address byte of next message
                sta ($0)
                inc $0          ;Move pointer to next position. No need to handle
                                ;carry since we print on the same line only.
                bra :-
@exit:        
;----
                ;Wait for user input
                jsr getkey
                
                pha             ;Save the keyboard input
                jsr appleii     ;Clear Screen and Show Apple II to
                                ;give feedback to user
                pla             ;Restore the keyboard input
                
                ;'C' is same as key 7 (Config Util)
                cmp #'C'|$80
                beq key7
                cmp #'c'|$80
                beq key7
                
                ;Esc key to reboot
                cmp #27|$80
                beq esckey
.ifdef IICP
                ;Space is same as key 8 (Toggle Speed)
                cmp #' '|$80 
                beq key8              
.endif
                ;Only key 2 - 8 is valid
                cmp #'2'|$80
                blt unknown
                cmp #'9'|$80
                bge unknown
                
                ;Convert ASCII to zero-based number
                ;i.e. '2'->0, '3'->1 and so on
                sec
                sbc #'2'|$80
                asl
                tax
                jmp (bmjmptable,x)
    
                ;-------------
                ;Unknown key is entered
unknown:        jsr bell        ;Warn the user
toagain:        bra again       ;Try again
                
                ;-----------
                ;Actions of keys
key2            := applesoft                
                
key3:           jsr enableromdisk
                jmp $c400
                
key4            := $c400              

key5            := $c500

key6            := $c600  
 
key8:           ;Toggle CPU Speed
.ifdef IICP
                sta rombank             ;the routine is in aux bank
                jsr toggleshowcpuspeedswrts
                bra toagain
.else
                bra unknown             
.endif                

esckey:         jmp pwrup               ;Cold Start (Ctrl-OA-Reset)

;///////////////////////////////////////////////////////////
;Load Control Panel from MegaFlash to RAM and execute
;The program is developed on CC65. CC65 apps
;are designed to run under DOS3.3 or ProDOS. So,
;we need some setup here.
;1) When the app finishes, its first check if ProDOS is present by
;   checking $BF00==$4C. If yes, it jumps to ProDOS Quit routine.
;   Otherwise,it jumps to $3D0 to return to DOS
;2) It uses HIMEM ($73-74) to setup its stack.

key7:           
                ;Set HIMEM to $bf00
                ld16i himem,$bf00
                
                ;Setup DOS warmstart vector
                ;to return to Boot Menu when the app finishes
                stz $bf00       ;Dont return to ProDOS
                lda #$4c        ;jmp opcode
                sta $3d0
                lda #.LOBYTE(bmstart)
                sta $3d1
                lda #.HIBYTE(bmstart)
                sta $3d2

                lda #MODE_LOADCPANEL
                jsr slxeq       ;Load Control Panel to Memory
                                ;A=0 if ok.
                tax             ;Test if a = 0
                bne nomf        ;A!=0, MegaFlash Not Exist
                jmp CPANELADDR

nomf:           ;No Megaflash
                ldx #NFMSGLEN-1         ;Length of notfoundmsg-1
:               lda notfoundmsg,x       ;show Not Found on screen
                sta LINE12+11,x
                dex
                bpl :-
                
                jsr getkey       
                jmp bmstart
                
                
notfoundmsg:     hascii "MegaFlash Not Found"                
NFMSGLEN        = (*-notfoundmsg)
 
;--------------------------
;A small routine to get keyboard input
;It also displays time regularly.
getkey:         
                .ifdef IICP
                lda acia1cmd    ;Touch slot 1 ($C09A) to slow down on IIC+
                .endif

                inc counter
                bne :+
                inc counter+1
                bne :+
                jsr displaytime
:                
                lda kbd
                bpl getkey      ;Wait Until kbd strobe bit is set
                stz kbdstrb     ;Clear kbd strobe bit
                rts 
                
;-------------------------- 
bmjmptable:     .addr key2         
                .addr key3
                .addr key4
                .addr key5
                .addr key6
                .addr key7
                .addr key8 

;--------------------------
; Menu Message
; The first two bytes are the screen memory address with high byte first.
; Since screen memory is at $400-$7FF, the MSB of high byte is never set.
; The text message is ASCII with MSB set. So, by checking the MSB, the
; start of next message can be detected.
;
; .dbyt marco defined 16-bit word with high byte first
;
                htab = 10
 menumsg:
                .dbyt (LINE5+htab)      
                hascii "2) Applesoft"
                
                .dbyt (LINE6+htab)
                hascii "3) Boot ROM Disk"
                
                .dbyt (LINE7+htab)
                hascii "4) Boot MegaFlash"
                
                .dbyt (LINE8+htab)
                hascii "5) Boot 3.5/SmartPort"
                
                .dbyt (LINE9+htab)
                hascii "6) Boot 5.25"
                
                .dbyt (LINE10+htab)
                hascii "7) Control Panel"                
                
.ifdef IICP                
                .dbyt (LINE11+htab)
                hascii "8) Toggle CPU Speed"                   
.endif           
           
                .dbyt (LINE21+(40-TITLESTRLEN)/2)       ;Centre the message       
titlestr:       hascii TITLESTR
TITLESTRLEN     = (*-titlestr)                
                
                .byte $00       ;End of Menu Message
                

;******************************************************************
; 
; MegaFlash routines
;
;******************************************************************

;--------------------------------------------------------------------
;Check if MegaFlash exists
;
;a=0 if exist, =1 if not exist
;
chkmegaflash:   lda idreg
                eor idreg       ;Acc = $ff if MegaFlash exists
                inc a           ;Acc = $00 if MegaFlash exists 
                rts

;------------------------------------------------------------------------------
; Enable ROM Risk
; Function: Tell MegaFlash to enable ROM Disk
; For Boot Menu Only since ROM Disk is at unit number 1. The unit number of 
; all disks are changed. So, it is expected that the computer to reboot.
enableromdisk:
                lda mfexist
                bne rts1

                lda #CMD_ENABLEROMDISK
                ;Fall-into execute

;--------------------------------------------------------------------
;Execute MegaFlash command
; A, X and Y remain unchanged
;
;Input: A = Command Code
;
;v=0 if no error, =1 if error
;          
execute:        sta cmdreg
                ;Wait until operation complete
:               bit statusreg
                bmi :- 
                rts

;--------------------------------------------------------------------
; Display Time
; Get a preformatted time string from MegaFlash
; then display on screen by copying to screen memory
;    
displaytime:
                lda mfexist
                bne rts1
                
                lda #CMD_GETTIMESTR
                jsr execute
         
                ;Copy the result to screen memory
                ldx #0
:               lda paramreg
                sta LINE24+32,x   ;x=32, y=23
                inx
                cpx #8
                bne :-
rts1:           rts


                .assert (*-BMRUN) <=512, error, "Boot Menu >512 bytes"
                
                