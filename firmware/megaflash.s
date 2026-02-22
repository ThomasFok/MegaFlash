;--------------------------------------------------------
; Apple IIc MegaFlash Firmware
; Module: MegaFlash Device Driver
; Version: 0.1
; Date: 28-Sep-2023

;*****************************************************************
; Buffer Pointer Reset
;
;Be careful when resetting the buffer pointer by stz cmdreg 
;instruction without checking the busy flag.
;
;For write access after the command such as
;    stz cmdreg
;    lda #spUnitNum
;    sta paramreg
;
;It should work reliably because the Pico PIO has a message queue. 
;Even the Pico CPU has not finished processing the first request 
;(stz cmdreg), the second request can be queued.
;
;For read access such as
;    stz cmdreg
;    lda paramreg
;
;The timing is more critical. It has caused a problem once at 4MHz 
;because the PIO CPU must finish the pointer reset command and send
;the updated value of parameter register to PIO before lda paramreg 
;instruction is executed.
;
;After tweaking the PIO code, the problem has been solved. But 
;it is still not a good practice because we may change the Pico code
;in future and the timing may be changed.
;
;The possible solutions are:
;1) Add some delay. i.e.
;    stz cmdreg
;    <do something>
;    lda paramreg
;
;Note: a single instruction delay may not be enough.
;
;2) Add a dummy I/O instruction
;    stz cmdreg
;    bit $C011
;    lda paramreg
;
;$C011 (RDLCBNK2) is a read-only softswtich address. Since it is 
;an I/O address, the bus cycle is executed at 1MHz. 
;
;3) Check the busy flag
;       stz cmdreg
;loop:  bit statusreg
;       bmi loop
;
;*****************************************************************





;**************************************************************************
; #     #                          #######                                 
; ##   ##  ######   ####     ##    #        #         ##     ####   #    # 
; # # # #  #       #    #   #  #   #        #        #  #   #       #    # 
; #  #  #  #####   #       #    #  #####    #       #    #   ####   ###### 
; #     #  #       #  ###  ######  #        #       ######       #  #    # 
; #     #  #       #    #  #    #  #        #       #    #  #    #  #    # 
; #     #  ######   ####   #    #  #        ######  #    #   ####   #    # 
;**************************************************************************

                .setcpu "65c02"
                .segment "ROM2"
                .reloc

                .include "buildflags.inc"
                .include "defines.inc"
                .include "macros.inc"


                ;
                ;imports
                ;
                ;From slotrom.s
                .import numbanks,pwrup,fpuenabled

                .import toshowbootmenu

                ;From smartport.s
                .importzp spCommand,spUnitNum,spBlockNum24,spIOPointer,spStatusListPtr,errorno
                .import putstatus,printayspc,printa
                .importzp zpscratch

                ;From dispatch.s
                .import dispatch
                .importzp aval,lcstate
                
                ;From bootmenu.s
                .import copybm
                

                
.ifdef IICP
                ;From accel.s
                .import setpowerupspeed
.endif                
                
                
                ;
                ;exports
                ;
                .export isonline,getdevstatus,getunitstatus,readblock,writeblock,coldstartinit,writeblocksizetovdh
                .export getdsb,getdib
                .export clockdriver,clockdriverimpl,loadcpanel
                
                ;.exportzp SPNUMDEV
                .if DEBUG
                .export print
                .endif
                

                ;
                ; Constants Definitions
                ; The .inc file is shared with Control Panel Project
                ;                
                .include "../common/defines.inc"

;
;Variables
;



;***********************************************************
;
; Print ASCII character in Acc for Debug Purpose
; 
;***********************************************************
                .segment "IOROM"
                .reloc
                
                .if DEBUG    
                .export print
print:          ;For MAME, if -bitbanger option is not enabled
                ;The data won't go out and transmitter is always
                ;busy. The program just hang while waiting the transmitter
                ;to be available. In that case, bit 6 and 5 of acia1status 
                ;are set. So, we just skip the print.
                phx             ;save X
                tax
                lda acia1status
                and #$60
                cmp #$60
                beq printexit   ;don't print if bit 6 and 5 are set

:               lda acia1status
                and #%00010000  ;transmitter busy?
                beq :-
                stx acia1data   ;Send the data
 printexit:     plx             ;restore x
                rts                     
                .endif


;***********************************************************
;
; Cold Start Initialization
;
;***********************************************************
                .segment "ROM1"
                .reloc
coldstartinit:           
                .if DEBUG
                ;Init Printer Port
                lda #$1f                ;Baud: $1f=19200, $1e=9600
                sta acia1ctrl
                lda #$0b                ;No interrupt
                sta acia1cmd
                lda acia1status         ;Clear IRQ Status
                .endif

                ;Enable MegaFlash by reading the Magic Address sequence
                lda MAGIC1
                lda MAGIC2
                lda MAGIC3
                lda MAGIC4
                lda MAGIC5               
          
                ;delay awhile for MegaFlash to switch mode
                ;It takes MegaFlash about 8us to switch mode
                jsr shortdelay
                
.if FPUSUPPORT
                ;FPU is not enabled if MegaFlash not exist
                stz fpuenabled
.endif
   
                ;Check if MegaFlash exist
                jsr chkmegaflashex
                bcs nomf
        
                ;Call ColdStart routine to inform MegaFlash
                ;we have coldstarted/rebooted
                ;It also retrieve config bytes so that we can
                ;configure the machine

                stz cmdreg              ;Reset buffer pointers
                lda #WE_KEY             ;Put Write Enable key to parameter buffer
                sta paramreg
                
                lda #CMD_COLDSTART
                jsr execute
                lda paramreg            ;configbyte1

                ;Check Auto-Boot from MegaFlash is enabled.
                ;If enabled, do nothing. Default is booting
                ;from slot 4.
                ;Otherwise, set $01 to $C5 for IIC+, $C6 for IIC
                bit #AUTOBOOTFLAG
                bne noautoboot          ;Branch if enabled
                ldx #NEXTBOOTSLOT       ;Skip slot 4
                stx $01
noautoboot:
                
.if FPUSUPPORT                
                ;Set MSB of fpuenabled if FPU is enabled in configbyte1
                ;fpuenabled has been set to 0 above
                bit #FPUFLAG            ;Test FPU Flag
                beq fpudisabled         ;Branch if not enabled
                dec fpuenabled          ;change it from 0 to $ff to set MSB
fpudisabled:
.endif

                ;Set Power Up CPU Speed
                ;
.ifdef IICP            
                ;A = configbyte1
                and #CPUSPEEDFLAG
                jsr setpowerupspeed
.endif   


nomf:
                ;Execute Boot Menu if enabled
                bit toshowbootmenu      ;Test MSB of toshowbootmenu
                bpl nobootmenu
                stz toshowbootmenu      ;Clear the MSB of toshowbootmenu
      
                jsr copybm              ;Copy Boot Menu code to RAM
                lda #.LOBYTE(BMRUN)     ;Execute Boot Menu
                ldy #.HIBYTE(BMRUN)     ;
                bra swjmp_ay            ;

nobootmenu:     ;exit here
                ;The original code at $BF19 is jmp($0000)

                ;Load ($0000) to A and Y
                ldy $01         ;Y=High Byte
                lda $00         ;A=Low Byte              
                ;fall into swjmp_ay
                
;----------------------------------------------------------
;JMP to Bank 0 address (AY)
;After coldstartinit, we need to jump back to main bank
;to start boot sequence or Boot Menu. This routine
;Reset stack pointer.
;Push the destination address-1 to stack. 
;Jump to swrts to Switch bank, then use RTS instruction
;to jump to the destination
;
; Input: A = Low byte of destination Address
;        Y = High byte of destination Address
;
;                
swjmp_ay:              
                tax             ;Test if A(Low Byte)=0
                bne :+          ;If not 0, dec low byte only
                dey             ;Dec High Byte
:               dec             ;Dec Low Byte
                ldx #$ff        ;Reset Stack Pointer
                txs             ;
                phy             ;Push High Byte
                pha             ;Push Low Byte
                jmp swrts       ;Switch bank, then RTS

;----------------------------------------------------------
;A short delay (18 cycles) sub-routine.
;must be in IOROM segment so that it is not accelerated
                .segment "IOROM"
                .reloc
shortdelay:     jsr :+
:               rts

;***********************************************************
;
; Device / Unit Status
; 
;***********************************************************          
     
;--------------------------------------------------------------------
;Check if MegaFlash exists
;
;c=0 if exist, =1 if not exist
;
                .segment "ROM2"
                .reloc
chkmegaflash:   lda idreg
                eor idreg       ;Acc = $ff if MegaFlash exists
                inc a           ;Acc = $00 if MegaFlash exists 
                
                cmp #$01        ;Setup Carry Flag
                rts
                
                
;--------------------------------------------------------------------
;Check if MegaFlash exists.
;It uses a more reliable but slow method to detect MegaFlash
;
;c=0 if exist, =1 if not exist
;         
                .segment "ROM2"
                .reloc
chkmegaflashex:
                lda #CMD_GETDEVINFO
                sta cmdreg              ;don't call execute. It may hang if MegaFlash not exists
        
                ;Wait until operation complete
                ;If MegaFlash does not exist,
                ;it is possible that the high bit(busy flag)
                ;may get stuck at 1.
                ;So, a loop counter is used to avoid dead loop
          
                ldx #100        ;A high value is needed to give Pico to complete the command        
:               bit statusreg
                bpl notbusy     ;Branch if busy flag is 0
                dex
                bne :-
 
                ;busy flag stuck. MegaFlash not exist
mfnotexist:     sec
                rts
        
notbusy:        ;Error Flag set?
                bvs mfnotexist  
        
                ;Compare the signature
                lda paramreg
                cmp #SIGNATURE1
                bne mfnotexist
                lda paramreg
                cmp #SIGNATURE2
                bne mfnotexist
                
                ;MegaFlash exist
                clc
                rts
                
             
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



                
;----------------------------------------------------------
; Check if the device is present
; It also install our clock driver in ProDOS.
;
; Output: set carry if device is offline
;         clear carry if device is online and working
;               
;Note: 
; This routine is called when ProDOS is booting and 
; scaning for drives. Device driver should do a 
; thorough check if the device is online. For 
; getdevstatus and getunitstatus routines, a quick
; check of device is preferred since they are called
; on every read/write call.
;
; Clock Driver
; The clock driver is in slotrom area. Since this routine
; is called when ProDOS is booting, we can install the 
; clock driver to ProDOS by patching the clock driver
; entry at $BF06.
isonline:       
                jsr chkmegaflashex
                bcs nomf2       ;carry set = Mega Flash not exist
                
                ;
                ;MegaFlash exists
                ;
                 
                ;ProDOS clock driver installation
                lda $bf06
                cmp #$60        ; rts opcode? If it is not, a clock driver already exist
                bne noinstall   ; branch to skip install
                
                lda #CMD_GETCONFIGBYTES
                jsr execute
                lda paramreg    ;Get configbyte1
                and #NTPCLIENTFLAG
                beq noinstall   ;branch if clock driver not enabled in Control Panel
                
                lda #.LOBYTE(clockdriver)
                sta $bf07
                lda #.HIBYTE(clockdriver)
                sta $bf08
                lda #$4c        ; jmp opcode
                sta $bf06
                
                ;Update MACHID Byte at $BF98 to indicate clock is present
setmachid:      lda #$01        ;Set bit 0
                tsb $bf98
                
noinstall:      ;---
                clc             ; MeagFlash exists
nomf2:          rts


;----------------------------------------------------------
;Get Device (Entire Smartport) Status
;
; Output: return number of unit(drive) in Acc
;         set carry if device is offline
;         clear carry if device is online and working
;               
getdevstatus:   jsr chkmegaflash
                bcs :+          ;Branch if MegaFlash not exists

                ;Execute GetDevStatus command
                lda #CMD_GETDEVSTATUS
                jsr execute
                
                ;Error Flag set?
                bvs :+
                
                ;No error, Read the unit count
                lda paramreg
                clc             ;Clear Error Flag
                rts        
          
                ;Error, Set Unit Count to 0
:               lda #0          ;Unit Count = 0
                sec             ;Error Flag
                rts

;----------------------------------------------------------
;Get Unit Status
;
; Input: spUnitNum (1-N) (Assume it is valid)
;
; Output: return block size in AXY 
;         set carry if unit is offline
;         clear carry if unit is online and working
;
getunitstatus:  jsr chkmegaflash        ;Check if MegaFlash exists
                bcs :+                  ;Branch if MegaFlash not exists
                
                ;Copy spUnitNum to Parameter Buffer
                stz cmdreg              ;reset buffer pointer
                lda spUnitNum
                sta paramreg
                                                                
                ;Execute GetUnitStatus command
                lda #CMD_GETUNITSTATUS
                jsr execute
                
                ;Error Flag set?
                bvs :+

                ;No error, Read the result
                lda paramreg            ;Block Count (Low Byte)
                ldx paramreg            ;Block Count (Mid Byte)
                ldy paramreg            ;Block Count (High Byte)
                clc                     ;No error
                rts                     

                ;Error!
:               lda #0                  ;Set Block Size to 0
                tax                     ;
                tay                     ;
                sec                     ;Set Error Flag
                rts

;--------------------------------------------------------------------
; Write Block Size to Block 2 (Volume Diretocry Header)
; Write Block size to offset $29-$2A of block 2 of the selected unit
; to work around the format bug of Copy II+ 8.4
;
; Input: spUnitNum
;                       
                .segment "ROM2"
                .reloc                                  
writeblocksizetovdh:
                stz cmdreg      ;Reset Buffer pointer
                lda spUnitNum   ;ProDOS unit number
                sta paramreg

                lda #WE_KEY     ;Write Enable Key
                sta paramreg

                lda #CMD_WRITEBLOCKSIZETOVDH
                jsr execute
                ;ignore error from MegaFlash
                ;since it is just a workaround
                ;of CopyII+ bug

                stz errorno     ;Success
                clc             ;Success Flag
                rts  

;----------------------------------------------------------
; Get DSB/DIB of unit
; Write DSB(Device Status Block) or Device Information Block
; to the location pointed by spStatusListPtr.
; DSB is same as the first 4 bytes of DIB
;
; Input: spUnitNum (1-N) (Assume it is valid)
;        spStatusListPtr
;
;        Carry Set if failed to retrieve DIB/DSB from Megaflash
;
                .segment "IOROM"        ;Must be in IOROM since we may switch LC area to RAM.
                .reloc
getdib:         ldx #DIB_LEN-2          ;We don't need the last two bytes
                                        ;which is Smartport Driver Version Word
                jsr getdibdsb
                bcs known_rts
                ;Overwrite offset 23,24 of DIB which is Smartport Driver Version
                ;It is defined in defines.inc of Apple Firmware, not Pico
                ldy #23                 ;offset 23
                lda #.LOBYTE(SPDRIVERVERSION)
                jsr putstatus
                iny                     ;y=24
                lda #.HIBYTE(SPDRIVERVERSION)
                jmp putstatus           ;jsr+rts       


getdsb:         ldx #DSB_LEN
                ;fall into getdibdsb
                 
;----------------------
;Subroutine of getdib and getdsb
;Get DIB or DSB from MegaFlash
;Input: X= Number of Bytes to be copied to destination (spStatusListPtr)
;
;Carry is the error flag.                 
getdibdsb:         
                jsr chkmegaflash        ;Check if MegaFlash exists
                bcs @error              ;Branch if MegaFlash not exists

                stz cmdreg              ;Reset Buffer Pointers
                lda spUnitNum           ;ProDOS unit number
                sta paramreg

                lda #CMD_GETDIB
                jsr execute
                bvs @error

                txa                     ;Save x to a
                ldx lcstate
                inc $c000,x             ;Restore LC
                
                ;Copy result, x is loop counter
                tax                     ;Restore x (loop counter)
                ldy #0
:               lda paramreg
                sta (spStatusListPtr),y
                iny
                dex   
                bne :-
                
                sta romain              ;Switch back to ROM ($D000-$FFFF)
                
                clc                     ;Success Flag                
                rts
                
@error:         sec                     ;Set Error Flag
known_rts:      rts                    


              

;***********************************************************
;
; Block Read / write
; 
;***********************************************************
                        

                
;--------------------------------------------------------------------
; Read Block from storage
;
; Input spUnitNum, spBlockNum24, spIOPointer
;
; Output: 1)Setup errorno
;           possible result:
;             SP_NOERR / SP_IOERR / SP_NODRVERR (No Device Connected)
;             They are the same for ProDOS and smartport
;          2) Carry Flag =0 if no error, =1 if error
;
; Assume the caller has validated all parameters and check the unit status
; before calling.
                .segment "ROM1"
                .reloc
readblock:
                ;Setup Parameters
                jsr rwparam

                ;execute ReadBlock command
                lda #CMD_READBLOCK
                jsr execute
                
                ;readoneblock routine destroys the error code in parameter buffer
                ;read the error code before calling readoneblock
                lda paramreg    ;error code
                sta errorno
                bne :+          ;If error, skip data copying

                ;No error, Copy Data from DataBuffer
                jsr readoneblock
                
                lda errorno
:               cmp #01         ;Setup Error Flag
                rts
       

;--------------------------------------------------------------------
; Sub-routine shared by ReadBlock and Write Block
;

;----------------
; Send Unit Number and Block Number to Parameter Buffer
rwparam:        stz cmdreg              ;reset buffer pointer
                lda spUnitNum           ;ProDOS unit number
                sta paramreg
                lda spBlockNum24        ;Block Number Low Byte
                sta paramreg
                lda spBlockNum24+1      ;Block Number Mid Byte
                sta paramreg
                lda spBlockNum24+2      ;Block Number High Byte
                sta paramreg            
                rts
                
;--------------------------------------------------------------------
; Write Block from storage
;
; Input spUnitNum, spBlockNum24, spIOPointer
;       
; Output: 1)Setup errorno
;           possible result:
;             SP_NOERR / SP_IOERR / SP_NODRVERR (No Device Connected)
;             SP_NOWRITEERR (Write Protected)
;             They are the same for ProDOS and smartport
;          2) Carry Flag =0 if no error, =1 if error
;
; Assume the caller has validated all parameters and check the unit status
; before calling.   
                .segment "ROM1"
                .reloc
writeblock: 
                ;Copy block to data buffer
                jsr writeoneblock       
                
                ;Setup Parameters
                jsr rwparam
                lda #WE_KEY                     ;Write Enable Key
                sta paramreg
                
                ;Execute WriteBlock command
                lda #CMD_WRITEBLOCK
                jsr execute
                
                lda paramreg                    ;error code
                sta errorno
                cmp #01                         ;Setup Error Flag
                rts

;*****************************************************************
;
;readoneblock / writeoneblock - RAM-based Implementation
;
;Speed Test:
;The time required (in usec) for reading one block is measured.
;
;           4MHz          4MHz           1MHz
;           (First run)   (Second run)
;RAM-Based  3772          3094           7130
;ROM-Based  6464          6381           7162
;
;Second run is faster because of the cache
;
;On a 1MHz machine (e.g. Apple IIc), RAM-Based implementation
;is actually slightly slower. But RAM-based implementation is
;used even on Apple IIc build of firmware because
;1) Both versions use the same implementation. Easier to track
;bugs or problems.
;2) Some users may have ZIP chips installed. The RAM-based 
;implementation is beneficial to them.
;
;*****************************************************************


;------------------------------------------------------------------------------
; Read One Block sub-routine           
; Function: Tansfer one block from MegaFlash data buffer to ProDOS    
;
; Input: spIOPointer (Source Address)         
;   
;
;The IIc plus accelerator does not cache memory region $C000-$CFFF. But the 
;data transfer routine cannot be placed in $D000-$FFFF because ProDOS data
;buffer is in the language card $D000-$FFFF. So, the data transfer routine
;must be in IOROM segment, which is not acclerated on IIc plus or 
;IIc with ZIP chip. Another solution is to put the routines in RAM.
;
;A section of zeropage, labelled as ramcodeloc, is used to store the routines.
;The content of the zeropage memory is preserved in parameter buffer. 
;Then, the data transfer routine is copied to that area.
;The routine is executed from RAM and the original content of that area
;is restored.
;
;Address $3A-$49 are used to store the RAM code. Refer to smartport.s for details
;Note: Interrupt is disabled when ProDOS calls us. So, we dont need to disable
;the interrupt when we use these memory locations.
;
                .segment "ROM1"
                .reloc
                ramcodeloc:= zpscratch
readoneblock:   
                lda #CMD_MODEINTERLEAVED        ;switch to interleaved mode
                sta cmdreg                      ;No need to poll busy flag
                                                ;We have a lot of work below
                
                ;Read spIOPointer before copying program code to zero page because
                ;they can be at the same memory address. Copying the code may
                ;destroy spIOPointer
                ldx spIOPointer         ;x=spIOPointer
                lda spIOPointer+1       ;read spIOPointer+1
                pha                     ;stack = spIOPointer+1
                
                ;save the original content and copy the data transfer routine rdramcode to zeropage
                stz cmdreg              ;Reset Buffer Pointers        
                ldy #RDRAMCODELEN             
:               lda ramcodeloc-1,y
                sta paramreg
                lda rdramcode-1,y
                sta ramcodeloc-1,y
                dey
                bne :-
                ;y=0 now. ramcodeexec expects y=0
                
                ;Reset Parameter Buffer pointer for restoring the content below               
                stz cmdreg              
                
                ;self modifying code, update the operands of sta instructions
                stx ramcodeloc+4        ;x=spIOPointer
                stx ramcodeloc+10
                pla                     ;pull spIOPointer+1 from stack
                sta ramcodeloc+5
                inc a                   ;upper page
                sta ramcodeloc+11

exec_restore:                
                jsr ramcodeexec         ;Switch to LC and execute the code

                ;restore the original content of zero page
                ;parameter pointer has been reset
                ldy #RDRAMCODELEN
:               lda paramreg       
                sta ramcodeloc-1,y   
                dey                
                bne :-           
                
                lda #CMD_MODELINEAR     ;Restore to linear mode
                sta cmdreg
                rts

;----------------------------------------------------------------------
rdramcode:      ;The code below is copied to ramcodeloc
                ;The address $ffff is modified to actual destination.
                ;It transfers one block from data buffer to RAM.
                lda datareg
                sta $ffff,y    ;Store to lower page,
                               ;$ffff is a placeholder of actual address
                lda datareg
                sta $ffff,y    ;Store to upper page
                               ;$ffff is a placeholder of actual address
                iny
                bne rdramcode
                rts
RDRAMCODELEN    = (* - rdramcode)                                                               
;----------------------------------------------------------------------



;----------------------------------------------------------------
; Sub-routine shared by readoneblock and writeoneblock           
                .segment "IOROM"
                .reloc         
ramcodeexec:
                ldx lcstate             ;Restore LC setting
                inc $c000,x             ;                
                ;ldy #0                 ;The data transfer routine expects y=0 before calling
                                        ;But Y already = 0, no need to set it up
                jsr ramcodeloc          ;execute the data transfer routine from zero page
                sta romain              ;Restore to ROM ($D000-$FFFF)
                rts

;------------------------------------------------------------------------------
; Write One Block sub-routine                
; Function: Transfer one block from ProDOS to MegaFlash data buffer
;
; Input: spIOPointer (Dest Address)         
; 
                .segment "ROM1"
                .reloc

writeoneblock:  lda #CMD_MODEINTERLEAVED        ;switch to interleaved mode, reset data buffer pointer
                sta cmdreg                      ;No need to poll busy flag
                                                ;We have a lot of work below

                ;Read spIOPointer to registers before copying program code to zero page because
                ;they can be at the same memory address. Copying the code may destroy spIOPointer
                ldx spIOPointer         ;x=spIOPointer
                lda spIOPointer+1       ;read spIOPointer+1
                pha                     ;stack = spIOPointer+1

                ;save the original content and copy the data transfer routine wrramcode to zeropage
                stz cmdreg              ;Reset Buffer Pointer
                ldy #WRRAMCODELEN
:               lda ramcodeloc-1,y
                sta paramreg
                lda wrramcode-1,y
                sta ramcodeloc-1,y
                dey
                bne :-
                ;y=0 now. ramcodeexec expects y=0
                
                ;Reset Parameter Buffer pointer for restoring the content below         
                stz cmdreg                     
                
                ;self modifying code, update the operands of lda instructions
                stx ramcodeloc+1        ;x=spIOPointer
                stx ramcodeloc+7
                pla                     ;pull spIOPointer+1 from stack 
                sta ramcodeloc+2
                inc a                   ;upper page
                sta ramcodeloc+8
 
                ;If WRRAMCODELEN and RDRAMCODELEN are the equal, the code
                ;section below is same as the one in readoneblock.
                ;Just reuse the code to save memory space.
                bra exec_restore
 ;-----
.if 0               
                jsr ramcodeexec         ;Switch to LC and execute the code
                     
                ;restore the original content of zero page
                ;parameter pointer has been reset
                ldy #WRRAMCODELEN
:               lda paramreg       
                sta ramcodeloc-1,y   
                dey                
                bne :-         

                lda #CMD_MODELINEAR     ;Restore to linear mode
                sta cmdreg                
                rts
.endif
;----

;----------------------------------------------------------------------
wrramcode:      ;The code below is copied to ramcodeloc.
                ;The address $ffff is modified to actual destination.
                ;It transfers one block from RAM to data buffer
                lda $ffff,y     ;Read from lower page
                                ;$ffff is a placeholder of actual address
                sta datareg
                lda $ffff,y     ;Read from upper page
                                ;$ffff is a placeholder of actual address
                sta datareg
                iny
                bne wrramcode
                rts
WRRAMCODELEN    = (* - wrramcode)     
;----------------------------------------------------------------------             


;*****************************************************************
;
;readoneblock / writeoneblock - ROM Only Implementation
;Loop is partially unrolled to improve speed.
;
;*****************************************************************

;------------------------------------------------------------------------------
; Read One Block sub-routine           
; Function: Tansfer one block from MegaFlash data buffer to ProDOS    
;
; Input: spIOPointer (Source Address)         
;   
.if 0   ;Not Used. The code is kept for future reference
                .segment "IOROM"        ;Must be in IOROM segment to access language card area      
                .reloc
readoneblock:   
                lda #CMD_MODELINEAR     ;switch to linear mode, reset data buffer pointer
                jsr execute
                
                ldx lcstate             ;Restore LC setting
                inc $c000,x             ;    
                jsr readonepage
                inc spIOPointer+1       ;next page
                jsr readonepage2        ;y already = 0       
                sta romain              ;Restore to ROM ($D000-$FFFF)
                rts
                
readonepage:    ldy #0                  ;Partially unroll  the loop
readonepage2:   lda datareg             ;Four bytes are transferred in each iteration
                sta (spIOPointer),y 
                iny
                lda datareg
                sta (spIOPointer),y
                iny
                lda datareg
                sta (spIOPointer),y
                iny
                lda datareg
                sta (spIOPointer),y
                iny
                bne readonepage2
                rts
.endif

;------------------------------------------------------------------------------
; Write One Block sub-routine                
; Function: Transfer one block from ProDOS to MegaFlash data buffer
;
; Input: spIOPointer (Dest Address)         
; 
.if 0   ;Not Used. The code is kept for future reference
                .segment "IOROM"        ;Must be in IOROM segment to access language card area              
                .reloc
writeoneblock:  
                lda #CMD_MODELINEAR     ;switch to linear mode, reset data buffer pointer
                jsr execute
                
                ldx lcstate             ;Restore LC setting
                inc $c000,x             ;    
                jsr writeonepage
                inc spIOPointer+1       ;next page
                jsr writeonepage2       ;y already = 0         
                sta romain              ;Restore to ROM ($D000-$FFFF)
                rts
                
writeonepage:   ldy #0                  ;Partially unroll  the loop
writeonepage2:  lda (spIOPointer),y     ;Four bytes are transferred in each iteration
                sta datareg
                iny
                lda (spIOPointer),y
                sta datareg
                iny
                lda (spIOPointer),y
                sta datareg
                iny
                lda (spIOPointer),y
                sta datareg
                iny                
                bne writeonepage2
                rts
.endif

;------------------------------------------------------------------------------
; ProDOS clock driver       
; Function: Update ProDOS current time in Global Page
; 
; Output: Date/Time in ProDOS Global Page is updated.
;         ($BF90-$BF93) for ProDOS < 2.5
;         ($BF8E-$BF93) for ProDOS 2.5 
;
                .segment "SLOTROM"
                .reloc
clockdriver:    lda #MODE_CLOCKDRV
                jmp slxeq       ;jsr + rts

                .segment "ROM2"
                .reloc
clockdriverimpl:                
                jsr chkmegaflash
                bcs @exit

                ;Assume ProDOS Ver <2.5
                lda #CMD_GETPRODOSTIME
                ldx #2          ;Write to $BF90
                
                ldy $bfff       ;Check ProDOS Version Byte
                cpy #$25
                blt :+          ;Branch if <$25
                ;ProDOS Ver >=2.5
                lda #CMD_GETPRODOS25TIME
                ldx #0          ;Write to $BF8E
:                
                ;Execute the command
                jsr execute
                bvs @exit       ;If error

                ;Copy the results
:               lda paramreg
                sta $bf8e,x
                inx
                cpx #6          
                bne :-
                
                ;Update MACHID Byte at $BF98 to indicate clock is present
                ;Note: The bit is set when the clock driver is installed.
                ;But the bit is reset afterwards. So, set this bit again here.
                jmp setmachid   ;jsr + rts
@exit:          rts



;------------------------------------------------------------------------------
; Load Control Panel Program Code to CPANELADDR
;
; Output: A=0 if ok, =1 if Megaflash does not exist
;
                .segment "ROM2"
                .reloc
                
dest            := $42  ;$42-43 destination pointer

loadcpanel:     stz aval                ;Assume No error        
                jsr chkmegaflash
                bcs @notexist

                ld16i dest, CPANELADDR
                ldx #0                  ;x = pageno
                ldy #0
                
@loop:          stz cmdreg              ;Reset Buffer Pointer
                lda #CMD_LOAD_CPANEL    ;Preload A=CMD_LOAD_CPANEL
                stx paramreg            ;Write current page number to parameter buffer
                
                jsr execute
                bvs @finish             ;error? Assume error means finish

                ;Copy one page
                ;y already = 0
:               lda datareg
                sta (dest),y
                iny
                lda datareg             ;copy two bytes in each iteration
                sta (dest),y
                iny
                bne :-
                ;y=0
                
                ;Inc pageno and pointer
                inx             ;inc pageno
                inc dest+1      ;Point to next page
                bra @loop
                
@notexist:      inc aval        ;Change it to 1 to indicate error           
@finish:        rts                
                