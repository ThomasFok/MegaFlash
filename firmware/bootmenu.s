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

                ;All codes default to this segment
                .define HOMESEGMENT "ROM2"

                .setcpu "65c02"
                .segment HOMESEGMENT
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
                
pwrup           := $FAA6
banger          := $C7C1                

                .segment "B0_FAC8"
                .reloc
bmentrypatch:   stz toshowbootmenu      ;Assume not display boot menu
                asl butn1               ;C-Flag = CA status
                bit butn0               ;N-Flag = OA status
                jmp bmentrypatch2       ;Continue there
                
                .segment "SLOTROM"
                .reloc
bmentrypatch2:  bmi oa_on               ;Branch if OA is pressed
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
                .segment HOMESEGMENT
                .reloc               
copybm:         
                ldx #0
@loop:          lda bmloc,x
                sta BMRUN,x
                lda bmloc+$100,x
                sta BMRUN+$100,x
                inx
                bne @loop
                rts

;********************************************************************
;
; Boot Menu Code
; Max Length: 512 Bytes
;
;********************************************************************
                .segment HOMESEGMENT
                .reloc
bmloc:                                  ;Physical Address of boot menu in ROM
                
                .org BMRUN              ;Switch to absolute code
tmpptr          := $02                  ;$02-$03 as temporary pointer
counter         := $04                  ;Use $04-05 as counter
mfexist         := $06                  ;=0 if megaflash exist


bmstart:        
                jsr appleii             ;Clear Screen and Show Apple IIc
                jsr chkmegaflash
                sta mfexist
                
                ;$00-$01 should point to the slot we are trying to boot.
                ;Otherwise, Unidisk 3.5 does not boot from boot menu on IIc+
                ;For example, to boot from slot 5, ($00) = $c500
                ;Initialize it to $c600
                stz $00
                lda #$c6
                sta $01
;--------------
; Display the menu on screen by writing to screen memory directly
;
again:    
                .ifdef IICP
                sta rombank             ;the routine is in aux bank                
                jsr showcpuspeedswrts   ;show CPU speed and return to main bank
                .endif
                
                ldx #0
                lda menumsg,x
nextmsg:        sta tmpptr+1    ;High Byte of addr
                inx
                lda menumsg,x   ;Low Byte of Addr
                sta tmpptr      ;

                ;Copy the message
:               inx             ;Next char
                lda menumsg,x   ;load char
                beq @exit       ;Zero = End of Messge     
                bpl nextmsg     ;MSB clear, it is address byte of next message
                sta (tmpptr)
                inc tmpptr      ;Move pointer to next position. No need to handle
                                ;carry since we print on the same line only.
                bra :-
@exit:        
;----
                ;Wait for user input and display time
                jsr getkey
                
                pha             ;Save the keyboard input
                jsr appleii     ;Clear Screen and Show Apple II to
                                ;give instant feedback to user
                pla             ;Restore the keyboard input
                
                ;Esc key to reboot
                cmp #27|$80
                bne @next
                jmp pwrup       ;Cold Start (Ctrl-OA-Reset)

@next:          
.ifdef IICP
                ;Space is same as S (Toggle CPU Speed)
                cmp #' '|$80 
                beq togglespeed             
.endif
                ;key 2-7?
                cmp #('7'+1)|$80
                bge notnumber                
                cmp #'2'|$80
                blt notnumber   ;bcc notnumber
                ;carry flag = 1

                ;Convert ASCII to zero-based number
                ;i.e. '2->0, '3'->1 and so on
                ;carry flag already set. SEC instruction not required
                sbc #'2'|$80
                
                ;Jump to handler with the jump table
                asl
                tax
                jmp (bmjmptable,x)

notnumber:      ;handle A-Z keys
                and #%11011111   ;clear bit 5 to convert lowercase letter to uppercase
                
.ifdef IICP   
                ;'S' key for toggle speed
                cmp #'S'|$80
                beq togglespeed
.endif                 
                
                ;'M' key for Monitor
                cmp #'M'|$80
                bne @next
                jmp monitor
                
                ;'C' key for control panel
@next:          cmp #'C'|$80
                beq cpanel
                ;fall into unknown
    
                ;-------------
                ;Unknown key is entered
unknown:        jsr bell        ;Warn the user
                bra again       ;Try again
                
;///////////////////////////////////////////////////////////
;Actions of keys
; 
 .ifdef IICP
togglespeed:    ;Toggle CPU Speed
                sta rombank             ;the routine is in aux bank
                jsr toggleshowcpuspeedswrts
                bra again
.endif   
                
boots4d2:       ;Boot from MegaFlash Drive 2 by simulating IN#4 cmd
                ;The bootcode of MegaFlash will boot from drive 2
                lda #$c4
                sta kswh
                bra boots4d1
                
bootromdisk:    jsr enableromdisk
                ;fall into boots4d1 (Boot from MegaFlash Drive 1)
                
                ;To boot from a slot, ($0) should point to the slot (e.g. $c600)
                ;$00-$01 is initialized to $c600
boots4d1:       dec $01         ;Set $01 to $c4 by dec it twice
boots5:         dec $01         ;Set $01 to $c5
boots6d1:       jmp ($00)

.ifdef IIC
boots6d2:       
                ;Boot 5.25 Drive 2 (IIc only, not work on IIc+)
                lda #$e0        ;Slot 6, Unit 2
                ldy #$01        ;y=1 to boot from drive 2
                ldx #$60        ;Slot 6
                ;duplicate the code from $C604-$C607
                stz $03
                stz $3c
                jmp $c60b
.endif
 
             

;///////////////////////////////////////////////////////////
;Load Control Panel from MegaFlash to RAM and execute
;The program is developed on CC65. CC65 apps
;are designed to run under DOS3.3 or ProDOS. So,
;we need some setup here.
;1) When the app finishes, its first check if ProDOS is present by
;   checking $BF00==$4C. If yes, it jumps to ProDOS Quit routine.
;   Otherwise,it jumps to $3D0 to return to DOS
;2) It uses HIMEM ($73-74) to setup its stack.

cpanel:           
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
;getkey
;A small routine to get keyboard input
;It also displays time regularly.
getkeyloop:         
                lda acia1cmd    ;Touch slot 1 ($C09A) to slow down on IIC+ and IIC with ZIP CHIP

                ;display time if counter = 0
                inc counter
                bne :+
                inc counter+1
                bne :+
getkey:         jsr displaytime
:        
                lda kbd
                bpl getkeyloop  ;Wait Until kbd strobe bit is set
                stz kbdstrb     ;Clear kbd strobe bit
                rts 
                
;-------------------------- 
;jump table
;key 2 - 7
bmjmptable:     .addr bootromdisk       ;key 2  
                .addr boots4d1          ;key 3
                .addr boots4d2          ;key 4
                .addr boots5            ;key 5
                .addr boots6d1          ;key 6
.ifdef IICP
                .addr unknown           ;key 7 for IIc+
.else
                .addr boots6d2          ;key 7 for IIc
.endif

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
.ifdef IICP     
                ;
                ;Menu for Apple IIc+
                ;
               .dbyt (LINE5+htab)   
                hascii "Boot from:"
                
                .dbyt (LINE6+htab+2)
                hascii "2) ROM Disk"
                
                .dbyt (LINE7+htab+2)
                hascii "3) MegaFlash Drv 1"
                
                .dbyt (LINE8+htab+2)
                hascii "4) MegaFlash Drv 2"                
                
                .dbyt (LINE9+htab+2)
                hascii "5) 3.5/SmartPort"
               
                .dbyt (LINE10+htab+2)
                hascii "6) Ext. 5.25"

                .dbyt (LINE12+htab)
                hascii "C) Control Panel"   

                .dbyt (LINE13+htab)      
                hascii "M) Monitor"
                       
                .dbyt (LINE14+htab)
                hascii "S) Toggle CPU Speed"        
                
.else           
                ;
                ;Menu for Apple IIc
                ;
                .dbyt (LINE4+htab)   
                hascii "Boot from:"
                
                .dbyt (LINE5+htab+2)
                hascii "2) ROM Disk"
                
                .dbyt (LINE6+htab+2)
                hascii "3) MegaFlash Drv 1"
                
                .dbyt (LINE7+htab+2)
                hascii "4) MegaFlash Drv 2"                
                
                .dbyt (LINE8+htab+2)
                hascii "5) 3.5/SmartPort"
               
                .dbyt (LINE9+htab+2)
                hascii "6) Int. 5.25"

                .dbyt (LINE10+htab+2)
                hascii "7) Ext. 5.25"        

                .dbyt (LINE12+htab)
                hascii "C) Control Panel"   

                .dbyt (LINE13+htab)      
                hascii "M) Monitor"
.endif           

                .dbyt (LINE21+(40-TITLESTRLEN)/2)       ;Centre the message       
titlestr:       hascii TITLESTR
TITLESTRLEN     = (*-titlestr)                
                
                .byte $00       ;End of Menu Message
                

;
; 
; MegaFlash routines for Boot Menu
;
;

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
                
;********************************************************************
;
; End of Boot Menu Code
;
;********************************************************************             