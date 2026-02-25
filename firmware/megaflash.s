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

                ;No contiguous memory segment can hold all the codes
                ;in this module. The code is separated to ROM1 and ROM2
                .define HOMESEGMENT "ROM1"

                .setcpu "65c02"
                .segment HOMESEGMENT
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
                .segment "DEBUG"
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
                .segment HOMESEGMENT
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
                lda #.LOBYTE(BMRUN-1)   ;Execute Boot Menu
                ldy #.HIBYTE(BMRUN-1)   ;
                bra swjmp               ;

nobootmenu:     ;exit here
                ;The original code at $BF19 is jmp($0000)
                ;Since this routine is in aux ROM bank,
                ;We cannot execute jmp($0000) directly.
                ;Use RTS instruction to jmp to the destination

                ;Load ($0000)-1 to A and Y
                ldy $01         ;Y=High Byte
                lda $00         ;A=Low Byte              
                bne :+          ;If not 0, dec low byte only
                dey             ;Dec High Byte
:               dec             ;Dec Low Byte
                ;fall into swjmp
                
;----------------------------------------------------------
;JMP to Bank 0 address
;After coldstartinit, we need to jump back to main bank
;to start boot sequence or Boot Menu. This routine
;Reset stack pointer.
;Push the destination address-1 to stack. 
;Jump to swrts to Switch bank, then use RTS instruction
;to jump to the destination
;
; Input: A = Low byte of destination Address-1
;        Y = High byte of destination Address-1
;
;                
swjmp:          ldx #$ff        ;Reset Stack Pointer
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
                .segment HOMESEGMENT    ;Restore th HOMESEGMENT
                .reloc

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
                .segment HOMESEGMENT
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
                .segment HOMESEGMENT
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
                .segment HOMESEGMENT
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
; Get DIB of unit
; Write Device Information Block (DIB)
; to the location pointed by spStatusListPtr.
; DIB is 25 bytes long
;
; Input: spUnitNum (1-N) (Assume it is valid)
;        spStatusListPtr
;
;        Carry Set if failed to retrieve DIB from Megaflash
;
                .segment HOMESEGMENT
                .reloc
getdib:         ldx #DIB_LEN-2          ;We don't need the last two bytes (offset 23, 24)
                                        ;which is Smartport Driver Version Word
                jsr getdibdsb
                bcs @error              ;carry set if error
                ;Overwrite offset 23,24 of DIB which is Smartport Driver Version
                ;It is defined in defines.inc of Apple Firmware, not Pico
                ldy #23                 ;offset 23
                lda #.LOBYTE(SPDRIVERVERSION)
                jsr putstatus
                iny                     ;offset=24
                lda #.HIBYTE(SPDRIVERVERSION)
                jmp putstatus           ;jsr+rts       
@error:         rts

;----------------------------------------------------------
; Get DSB of unit
; Write Device Status Block (DSB)
; to the location pointed by spStatusListPtr.
; DSB is 4 bytes long and they are same as the first 4 bytes
; of DIB
;
; Input: spUnitNum (1-N) (Assume it is valid)
;        spStatusListPtr
;
;        Carry Set if failed to retrieve DSB from Megaflash
;
                .segment "IOROM"        ;Must be in IOROM since we fall into getdibdsb
                .reloc
getdsb:         ldx #DSB_LEN
                ;fall into getdibdsb
                 
;----------------------
;Subroutine of getdib and getdsb
;Get DIB or DSB from MegaFlash
;Input: X= Number of Bytes to be copied to destination (spStatusListPtr)
;
;Carry is the error flag.   
                .segment "IOROM"        ;Must be in IOROM since we restore LC setting
                .reloc
getdibdsb:      jsr chkmegaflash        ;Check if MegaFlash exists
                bcs @error              ;Branch if MegaFlash not exists

                stz cmdreg              ;Reset Buffer Pointers
                lda spUnitNum           ;ProDOS unit number
                sta paramreg

                lda #CMD_GETDIB
                jsr execute
                bvs @error

                txa                     ;Save x to a
                ldx lcstate             ;Restore LC
                inc $c000,x             ;
                
                ;Copy result, x is loop counter
                tax                     ;Restore x (loop counter)
                ldy #0
@loop:          lda paramreg
                sta (spStatusListPtr),y
                iny
                dex   
                bne @loop
                
                sta romain              ;Switch back to ROM ($D000-$FFFF)
                
                clc                     ;Success Flag                
                rts
                
@error:         sec                     ;Set Error Flag
                rts                    

;***********************************************************
;
; ######                 #####  
; #     #  ####  #    # #     # 
; #     # #    # ##  ##       # 
; ######  #    # # ## #  #####  
; #   #   #    # #    # #       
; #    #  #    # #    # #       
; #     #  ####  #    # ####### 
; 
;***********************************************************


                ;From this point, all codes default to ROM2 segment
                .undef  HOMESEGMENT
                .define HOMESEGMENT "ROM2"
              

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
                .segment HOMESEGMENT
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
                .segment HOMESEGMENT
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
;
;The IIc plus accelerator does not cache memory region $C000-$CFFF. But the 
;data transfer routine cannot be placed in $D000-$FFFF because ProDOS data
;buffer is in the language card $D000-$FFFF. So, the data transfer routine
;must be in IOROM segment, which is not acclerated on IIc plus or 
;IIc with ZIP chip. Another solution is to put the routine in RAM.
;
;Stack memory area ($100-$1FF) is used to store the data transfer routine.
;The advantage is that the content of the memory does not need to be preserved.
;The data transfer routine is pushed to the stack and execute from there
;
;But the stack may not have enough space to store the entire routine.
;The workaround is if there is not enough space, fall back to ROM based routine
;
;Speed Test
;==========
;
;The time required (in usec) for reading one block is measured.
;
;           4MHz          4MHz           1MHz
;           (First run)   (Second run)
;Stack      3629          3112           6807   (Code in $100-$1FF)
;RAM-Based  3691          3190           7119   (Code in zero page, Firmware Version 1.0)
;ROM-Based  6513          6407           7194   (Code in IOROM segment, paritally unrolled)
;
;
;Second run is faster because of the cache
;
;Stack Free Space Calculation
;============================
;
;We reserve some more bytes from the stack when we check if the program
;fits in the stack in case there is NMI interrupt handler or some 
;programs use the RAM area near $100.
;
; Stack Free Space = Stack Pointer + 1
;
; So, the test is
; SP+1 >=  Required Space
; SP   >= (Required Space)-1
;
;*****************************************************************

;Make sure the stack has at least these amount of free space
STACKRESERVEDBYTE       =       20


;------------------------------------------------------------------------------
; Read One Block sub-routine (ROM-only Implementation)          
; Function: Tansfer one block from MegaFlash data buffer to ProDOS    
;
; Input: spIOPointer (Source Address)         
;   
                .segment "IOROM"        ;Must be in IOROM segment to access language card area      
                .reloc
readoneblock_rom:   
                ;no need to reset data buffer pointer
                ;The pointer is reset by MegaFlash after Read Block command
                
                ldx lcstate             ;Restore LC setting
                inc $c000,x             ;    
                jsr readonepage
                inc spIOPointer+1       ;next page
                jsr readonepage2        ;y already = 0       
                sta romain              ;Restore to ROM ($D000-$FFFF)
                rts
;---                
readonepage:    ldy #0                  
readonepage2:   lda datareg             ;Two bytes are transferred in each iteration
                sta (spIOPointer),y 
                iny
                lda datareg
                sta (spIOPointer),y 
                iny                
                bne readonepage2
                rts


;------------------------------------------------------------------------------
; Read One Block sub-routine           
; Function: Tansfer one block from MegaFlash data buffer to ProDOS    
;
; Input: spIOPointer (Source Address)         
;   
;
                .segment HOMESEGMENT
                .reloc
                ramcodeloc:= zpscratch
readoneblock:   
                ;Check if there is enough space in stack
                ;fall back to ROM-only implementation if not enough space                
                tsx
                cpx #RDRAMCODELEN+STACKRESERVEDBYTE-1
                bcs @start                      ;bge @start. **read notes below
                jmp readoneblock_rom            ;in IOROM segment

@start:         ;** C=1
                ;The ADC instruction below assumes C=1

                lda #CMD_MODEINTERLEAVED        ;switch to interleaved mode
                sta cmdreg                      
;---             
                ;
                ;Copy the data transfer routine to stack
                ;
                ldx     #RDRAMCODELEN
:               lda     rdramcode-1,x
                pha
                dex
                bne     :-
                ; SP->
                ;  +1: First Byte of RAM code                
                
                ;Patch the self modified Code
                ;
                tsx                     ;x=Current SP
                lda     spIOPointer     ;Low Byte
                sta     $101+4,x        ;offset 4
                sta     $101+10,x       ;offset 10
                lda     spIOPointer+1   ;High Byte
                sta     $101+5,x        ;offset 5
                inc                     ;next page
                sta     $101+11,x       ;offset 11

                ;X=Current SP
                txa                     ;Save a copy to A
                
                ;spIOPointer is not needed after patching the code
                ;Store the entry point of the code to spIOPointer
                ;Then, use jmp (spIOPointer) to execute the code
                inx                     ;Adjust Low Byte
                stx     spIOPointer     ;Low Byte
                ldy     #$01            ;High Byte
                sty     spIOPointer+1   
                ;y=1
                
                ;Restore SP to original value
                ;Add RDRAMCODELEN to A
                ;Since C=1 (see notes above), we add RDRAMCODELEN-1 without clearing Carry Flag
                adc #RDRAMCODELEN-1
                tax                    
                txs                   
                
                ;
                ;The code is ready to be executed
                ;
                
                ;Continue in IOROM segment
                dey             ;Set y=0. The data transfer routine expects y=0 before calling
                jmp execramcode

;----------------------------------------------------------------------
rdramcode:      ;The code below is copied to stack area ($100-$1FF)
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
                jmp ramcode_rtn
RDRAMCODELEN    = (* - rdramcode)                                                               
;----------------------------------------------------------------------

;----------------------------------------------------------------------
; Sub-routine shared by readoneblock and writeoneblock      
; must be in IOROM segment     
                .segment "IOROM"
                .reloc    
execramcode:
                ldx lcstate             ;Restore LC setting
                inc $c000,x             ;                

                ;Jmp to data transfer routine
                jmp (spIOPointer)
                
ramcode_rtn:    ;The data transfer routine jump back here             
                lda #CMD_MODELINEAR     ;Restore to linear mode
                sta cmdreg
                sta romain              ;Restore to ROM ($D000-$FFFF)                
                rts
;----------------------------------------------------------------------                

;------------------------------------------------------------------------------
; Write One Block sub-routine (ROM-only Implementation)        
; Function: Transfer one block from ProDOS to MegaFlash data buffer
;
; Input: spIOPointer (Dest Address)         
; 
                .segment "IOROM"        ;Must be in IOROM segment to access language card area              
                .reloc
writeoneblock_rom:  
                stz cmdreg              ;reset data buffer pointer
                
                ldx lcstate             ;Restore LC setting
                inc $c000,x             ;    
                jsr writeonepage
                inc spIOPointer+1       ;next page
                jsr writeonepage2       ;y already = 0         
                sta romain              ;Restore to ROM ($D000-$FFFF)
                rts
                
writeonepage:   ldy #0                  
writeonepage2:  lda (spIOPointer),y     ;Two bytes are transferred in each iteration
                sta datareg
                iny
                lda (spIOPointer),y
                sta datareg
                iny                
                bne writeonepage2
                rts

;------------------------------------------------------------------------------
; Write One Block sub-routine                
; Function: Transfer one block from ProDOS to MegaFlash data buffer
;
; Input: spIOPointer (Dest Address)         
; 
                .segment HOMESEGMENT
                .reloc

writeoneblock:  
                ;Check if there is enough space in stack
                ;fall back to ROM-only implementation if not enough space
                tsx
                cpx #RDRAMCODELEN+STACKRESERVEDBYTE-1
                bcs @start                      ;bge @start. **read notes below
                jmp writeoneblock_rom           ;in IOROM segment

@start:         ;** C=1
                ;The ADC instruction below assumes C=1

                lda #CMD_MODEINTERLEAVED        ;switch to interleaved mode
                sta cmdreg                                     
;---             
                ;
                ;Copy the data transfer routine to stack
                ;
                ldx     #WRRAMCODELEN
:               lda     wrramcode-1,x
                pha
                dex
                bne     :-
                ; SP->
                ;  +1: First Byte of RAM code                      
                
                ;Patch the self modified Code
                ;
                tsx                     ;x=Current SP
                lda     spIOPointer     ;Low Byte
                sta     $101+1,x        ;offset 1
                sta     $101+7,x        ;offset 7
                lda     spIOPointer+1   ;High Byte
                sta     $101+2,x        ;offset 2
                inc                     ;next page
                sta     $101+8,x        ;offset 8

                ;X=Current SP
                txa                     ;Save a copy to A
                
                ;spIOPointer is not needed after patching the code
                ;Store the entry point of the code to spIOPointer
                ;Then, use jmp (spIOPointer) to execute the code
                inx                     ;Adjust Low Byte
                stx     spIOPointer     ;Low Byte
                ldy     #$01            ;High Byte
                sty     spIOPointer+1   
                ;y=1
                
                ;Restore SP to original value
                ;Add WRRAMCODELEN to A
                ;Since C=1 (see notes above), we add WRRAMCODELEN-1 without clearing Carry Flag
                adc #WRRAMCODELEN-1
                tax                    
                txs                    
                
                ;
                ;The code is ready to be executed
                ;
                
                ;Continue in IOROM segment
                dey             ;Set y=0. The data transfer routine expects y=0 before calling
                jmp execramcode

;----------------------------------------------------------------------
wrramcode:      ;The code below is copied to stack area ($100-$1FF)
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
                jmp ramcode_rtn
WRRAMCODELEN    = (* - wrramcode)     
;----------------------------------------------------------------------             




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

                .segment HOMESEGMENT
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
; Output: aval=0 if ok, =1 if Megaflash does not exist
;
                .segment HOMESEGMENT
                .reloc
                
dest            := $42  ;$42-43 destination pointer

loadcpanel:     stz aval                ;Assume No error        
                jsr chkmegaflash
                bcs @notexist

                ld16i dest, CPANELADDR
                ldx #0                  ;x = pageno
                ldy #0                  ;The code below expects y=0
                
                ;Reset Parameter Buffer Pointer for first Page
                ;CMD_LOAD_CPANEL command resets the pointer.
                ;So, no need to put this instruction inside the loop
                stz cmdreg              
          
@loop:          lda #CMD_LOAD_CPANEL    ;Preload A=CMD_LOAD_CPANEL
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

