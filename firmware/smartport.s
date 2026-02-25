;--------------------------------------------------------
; Apple IIc MegaFlash Firmware
; Module: Smartport Core Implementation
;

                ;All codes default to this segment except debug routines
                .define HOMESEGMENT "ROM1"

                .setcpu "65c02"
                .segment HOMESEGMENT
                .reloc

                .include "buildflags.inc"
                .include "defines.inc"
                .include "macros.inc"


                ;
                ; Imports
                ;

                ;From Device Driver
                .import isonline,getdevstatus,getunitstatus,readblock,writeblock,writeblocksizetovdh
                .import getdsb,getdib
                .if DEBUG
                .import print
                .endif
                
                ;From slotrom.s
                .importzp SLOTX16

                ;From dispatch.s
                .importzp aval,xval,yval,lcstate
 
                ;
                ; Exports
                ;
                .exportzp spUnitNum,spBlockNum24,spIOPointer,spStatusListPtr,errorno
                .exportzp zpscratch,zpstart     ;Zero Page segment address
                .export putstatus
                .export p8spentry
                
                .if DEBUG
                .export printa,printayspc,prnerr
                .endif

;
; Constants
;
SPPARAMLEN      EQU     7       ;Length of Smartport Parameter List.
                                ;Since we don't handle Read/Write Call. The len is 7 bytes.

;-------------------------------------------------------------------------
;
; Zero Page Allocation
;
; This Smartport module requires 7 bytes in zeropage as working area.
; 6 bytes for Smartport Parameter and Working Area.
; 1 byte for storing errorcode
;
; The memory is saved in stack and restored. It can be at any memory locations
; unless the locations are used by interrupt service routine. Test program
; shows that interrupt is disabled when ProDOS calls us. But it is not guaranteed
; that interrupt is disabled in all circumstances.
;
; According to ProDOS Technical Reference Section 3.3.1 and Exploring
; Apple GS/OS and ProDOS 8 Page 54, $3A-$4E are used by ProDOS. Application 
; software and ISR should avoid this region. This area is a good place for zero page
; variables.
; 
; $3A-$3F are used by ProDOS 8 disk driver for 5.25 drive and /RAM as 
; TEMPORARY variable. $42-$47 are used to pass parameters to disk driver (i.e. us).
; This region should be avoid.
;
; So, the zeropage is divided into two segments.
;
; ZPSCRATCH segment is $3A-$3F (6 bytes). Perfect fit for storing 
; Smartport Parameter. This area is not restored unless RESTOREZPSCRATCH 
; is set to TRUE.
;
; ZEROPAGE segment is $48-$4E (7 bytes). This area is restored.
;




;---------------------
; spParamList
;
; SmartPort parameters are copied to spParamList. The length of Smartpport
; parameters is 7 bytes. But the first byte is Parameter Count. 
; This byte is not used after its value is validated. To reduce 
; zero page memory usage, this byte is not stored. So, the length of
; spParamList is 6 bytes

                .segment "ZPSCRATCH":zeropage
                .reloc
zpscratch:     
spParamList:    .res SPPARAMLEN-1       ;Smartport Parameters are copied here 
ZPSCRATCHSIZE = (*-zpscratch)

                 .segment  "ZEROPAGE":zeropage
                .reloc
zpstart:                
errorno:        .res 1                  ;Result Code
ZPSIZE = (*-zpstart)


;Shared variables
spCommand      := yval                  ;Smartport Command Code
                                        ;Not used after command is dispatched.
                                        ;Share with yval to save memory.
spParamListPtr :=  spParamList          ;Pointer to access parameter list
                                        ;To save memory, it is at the first two bytes
                                        ;of spParamList. See copyparam routine


;
;Label to access parameter. The number is the offset.
;Note: Parameter Count is not stored to spParamList
;
spUnitNum       := spParamList+0        ;1 Byte Unit Number (1-N)
spStatusListPtr := spParamList+1        ;2 Bytes Status List Pointer
spStatusCode    := spParamList+3        ;1 Byte Status Code
spControlCode   := spParamList+3        ;1 Byte Control Code
spBlockNum24    := spParamList+3        ;3 Bytes Block Number
spIOPointer     := spParamList+1        ;2 Bytes IO Buffer Pointer


;**************************************************************
; #######                               
; #        #    #  #####  #####   #   # 
; #        ##   #    #    #    #   # #  
; #####    # #  #    #    #    #    #   
; #        #  # #    #    #####     #   
; #        #   ##    #    #   #     #   
; #######  #    #    #    #    #    #   
;**************************************************************
      
                .segment HOMESEGMENT
                .reloc


;-----------------------------------------------
;Entry point for ProDOS and Smartport Driver
;Zero Page Storage Area is saved to stack
;Then, dispatch to ProDOS or Smartport Driver
;Restore the Zero Page Content
;
p8spentry:      
                ;
                ; Save content of ZEROPAGE Segment to stack
                ;
.if ZPSIZE=1
                ;We dont need a loop if ZPSIZE is 1.
                lda zpstart
                pha
.else
                ;Save whole ZP Storage Area into stack
                ldx #ZPSIZE-1
:               lda zpstart,x
                pha
                dex
                bpl :-
                .assert ZPSIZE-1<=$80, error, "bpl range error"
.endif

                ;
                ; Save content of ZPSCRACTCH Segment to stack
                ; if RESTOREZPSCRATCH is TRUE
                ;
.if RESTOREZPSCRATCH
                ;Save whole ZP Storage Area into stack
                ldx #ZPSCRATCHSIZE-1
:               lda zpscratch,x
                pha
                dex
                bpl :-
                .assert ZPSCRATCHSIZE-1<=$80, error, "bpl range error"
.endif

                ;------------------------
                bit aval        ;aval is mode, MSB set for Smartport
                bmi tosp
                jsr p8driver
                bra exit
tosp:           jsr spdriver

exit:           .if DEBUG
                jsr prnerr      ;Print Result Code (errorno)
                .endif

                ;copy errorno to aval
                lda errorno
                sta aval
                ;-----------------------
                
                ;
                ; Restore ZPSCRATCH segment from stack
                ; if RESTOREZPSCRATCH is TRUE
                ;
.if RESTOREZPSCRATCH
;If we offset the x-register by -ZPSCRATCHSIZE and add ZPSCRATCHSIZE to
;address of sta instruction. The code becomes:
;
;                Original Code                  New Code
;                ldx #0                         ldx #0-ZPSCRATCHSIZE
;@loop:          pla                            pla
;                sta zpscratch,x                sta zpscratch+ZPSCRATCHSIZE,x
;                inx                            inx
;                cpx #ZPSCRATCHSIZE             cpx #ZPSCRATCHSIZE-ZPSCRATCHSIZE
;                bne @loop                      bne @loop
;
;The cpx instruction is not needed since it is cpx #0.
;
                ldx #.LOBYTE(0-ZPSCRATCHSIZE)   ;.LOBYTE() is needed for negative number
@loop:          pla
                sta z:zpscratch+ZPSCRATCHSIZE,x
                inx
                bne @loop 
.endif

                ;
                ; Restore ZEROPAGE Segment from stack
                ;
.if ZPSIZE=1
                ;We dont need a loop if ZPSIZE is 1.
                pla
                sta zpstart
.else
                ;Restore whole ZP Storage Area
                ldx #0
:               pla
                sta zpstart,x
                inx
                cpx #ZPSIZE
                bne :-                
.endif
                rts

                
;****************************************************************                       
; ######                   ######   #######   #####  
; #     #  #####    ####   #     #  #     #  #     # 
; #     #  #    #  #    #  #     #  #     #  #       
; ######   #    #  #    #  #     #  #     #   #####  
; #        #####   #    #  #     #  #     #        # 
; #        #   #   #    #  #     #  #     #  #     # 
; #        #    #   ####   ######   #######   #####             
;****************************************************************               


                
;*********************************************************
;
; ProDOS Block Device Driver Implementation
;
; Output: Setup errorno, xval and yval before return
;
;*********************************************************
                .segment HOMESEGMENT
                .reloc
p8driver:
                .if DEBUG
                jsr prnpdparam
                .endif
;
; Start of ProDOS Block Deivce Driver
;
                ;Validate Unit Number
                lda pdUnitNumber
                and #%01110000          ;Get Slot Number
                cmp #SLOTX16            ;same as our slot?
                beq :+                  ;branch if yes

                ;Bad Unit Number
p8ioerr:        lda #PD_IOERR
                sta errorno
                rts
                        
:               ;Setup spUnitNum (1-N)
                ;=1 if MSB of pdUnitNumber=0, =2 if MSB of pdUnitNumber = 1
                .if 0                   ;Two Drives per slot only
                lda pdUnitNumber
                asl a			;Move MSB of Unit Number to carry
                lda #1			;Acc = 1
                adc #0			;Add carry into Acc
                sta spUnitNum
                .endif
                
                ;8 Drives Per slot for ProDOS 2.5 Compatibility
                lda pdUnitNumber
                asl a			;Move MSB of Unit Number to carry
                and #7
                adc #1			;Add carry into Acc  
                sta spUnitNum
                        
                ;Validate and Dispatch Command
                lda pdCommandCode
                beq pstatus             ;Branch if Command 0 (PD_STATUS)
                cmp #4                  ;Only 0-3 is valid
                bge p8ioerr
                cmp #PD_FORMAT
                bne preadwrite          ;Not branch if Commnad PD_FORMAT
pformat:        ;ProDOS format command
                ;same implementation as pstatus

pstatus:        ;ProDOS status command
                ;
                jsr isonline
                bcs p8nodrverr
                jsr getunitstatus       ;return block size in AXY
                bcs p8nodrverr  
                
                ;block size should be returned to ProDOS in XY
                ;if Y!=0, capacity of the unit >$FFFF,
                ;then set block size to $FFFF
                cpy #0
                beq :+
                lda #$ff        ;y!=0, set a and x to $FF
                tax             ;
                
:               sta xval
                stx yval
                
p8success:      stz errorno
                rts

p8nodrverr:     lda #PD_NODEVERR        ;No Device Connected
                sta errorno
                rts

preadwrite:     ;read/write command

                jsr getunitstatus       ;Get Block Size in AXY
                bcs p8nodrverr
                        
                ;Validate block number
                ;Block Size (AXY) > Block Number
                ;
                ;if y!=0, Block Size>$FFFF
                ;block number must be valid.
                ;If not, check  AX > pdBlockNum                   
                ;Test: AX - pdBlockNum -1 >= 0
                cpy #0
                bne gdpdblk
                
                ;calculate AX - pdBlockNUm -1
                                        ;A = Low Byte of Block Size
                clc                     ;minus one
                sbc pdBlockNum
                txa                     ;A = Mid Byte of Block Size
                sbc pdBlockNum+1
                bge gdpdblk
                
                ;Bad block number
pioerr:         lda #PD_IOERR
                sta errorno
                rts
                
gdpdblk:        ;copy pdBlockNum to spBlockNum24
                mov16 spBlockNum24,pdBlockNum
                stz spBlockNum24+2        ;Set High Byte to zero

                ;copy pdIOBuffer to spIOPointer
                mov16 spIOPointer,pdIOBuffer

                ;Call ReadBlock or WriteBlock
                lda pdCommandCode
                cmp #PD_WRITE
                beq pwrite
                jmp readblock                           
pwrite:         jmp writeblock


;**************************************************************************     
;  #####                                  ######                         
; #     #  #    #    ##    #####   #####  #     #   ####   #####   ##### 
; #        ##  ##   #  #   #    #    #    #     #  #    #  #    #    #   
;  #####   # ## #  #    #  #    #    #    ######   #    #  #    #    #   
;       #  #    #  ######  #####     #    #        #    #  #####     #   
; #     #  #    #  #    #  #   #     #    #        #    #  #   #     #   
;  #####   #    #  #    #  #    #    #    #         ####   #    #    #   
;**************************************************************************     


;*********************************************************
;
;  Smartport Driver Implementation
;
;  Input: xval = Stack Pointer Value when the driver is called.
;  Output: Setup errorno, xval and yval before return
;
;*********************************************************
                .segment HOMESEGMENT
                .reloc
spdriver:       ;errorno is the value returned in Acc
                ;Default to BadCmd (Not implemented) error
                lda #SP_BADCMDERR
                sta errorno
                        
                ldx xval                ;original stack pointer
                        
                ;Copy Return Addres to spParamListPtr, which acts as a temporary pointer
                ;Also Add 3 to return address to skip the call parameters when we return to caller
                lda $101,x              ;Return Address Low Byte
                sta spParamListPtr      ;Copy to spParamListPtr
                clc                     ;Add 3 to return address
                adc #3
                sta $101,x              ;Overwrite original Return Address Low Byte
                lda $102,x              ;Return Address High Byte
                sta spParamListPtr+1    ;Copy to spParamListPtr High Byte
                adc #0                  ;Add carry
                sta $102,x              ;Overwrite original Return Address High Byte

                ;Now, spParamListPtr point to last byte of JSR instruction
                ;Copy SmartPort Command and Parameters address to spCommand and spParamListPtr
                ldy #1
                jsr getparam            ;Get Commad Number
                sta spCommand           ;Store it
                iny                     ;y=2
                
                ;Read both low and high bytes to registers before
                ;writing to spParamListPtr because getparam
                ;is using spParamListPtr.
                ;Note: getparam destroys x-register
                jsr getparam            ;A=Low-byte of parameter list address
                pha                     ;push to stack
                iny                     ;y=3
                jsr getparam            ;A=High-byte of parameter list address
                sta spParamListPtr+1    ;Store High Byte    
                pla                     ;Restore low byte from stack
                sta spParamListPtr      ;Store low byte
                ;Now spParamListPtr point to actual parameters
                        
                ;Copy Parameter List to spParamList. spParamListPtr is destroyed.
                ;Return Parameter Count in A
                jsr copyparam
                
                .if DEBUG
                pha                     ;Save Paramter Count
                jsr prnspparam          ;Printout Call Parameter
                pla                     ;Restore it
                .endif
                        
                ;
                ;Validate Parameter Count in A
                ;
                ldx spCommand
                cmp sp_pcount_table,x   ;x=command
                beq pcountok
                lda #SP_BADPCNTERR      ;Bad Parameter Count
                sta errorno
known_rts:      rts             
                
;Smartport Parameter Count OK           
pcountok:
                ;Validate spCommand in x
                ;Only Command $00-$05 is implemented
                cpx #06
                bge not_implemented

                ;
                ;Dispatch Command
                ;
                txa             ;Copy spCommand from x to a
                asl             ;times 2
                tax
                jmp (sp_dispatch_table,x)
                

                ;Do Nothing, Use Default Action, return SP_BADCMDERR
not_implemented:= known_rts     ;Point it to the nearest rts to save memory
                        
sp_dispatch_table:
                .addr sp_status
                .addr sp_readblock
                .addr sp_writeblock
                .addr sp_format
                .addr sp_control
                .addr sp_init
                        
                        
sp_pcount_table:
                .byte 3   ;0 = status
                .byte 3   ;1 = read
                .byte 3   ;2 = write
                .byte 1   ;3 = format
                .byte 3   ;4 = control
                .byte 1   ;5 = init
                .byte 1   ;6 = open
                .byte 1   ;7 = close
                .byte 4   ;8 = read
                .byte 4   ;9 = write
                        
;*********************************************************
; Smartport Status Command Handler
; Further Dispatch by Unit Number
; Then, decide what to do by Status Code
;
sp_status:              
                ;Put Status Code to X
                ldx spStatusCode

                ;Further Dispatch by unit number
                lda spUnitNum
                bne sp_status_unit

                ;
                ;Handle Unit Number = 0, Get Status of host
                ;
                
                ;Check Status Code in x
                txa                     ;Update Z-Flag
                beq sp_status_u0_sc0    ;Status Code = 0?
                
                ;Only Status Code 0 is valid for host
                ;report Bad Status Code Error
                lda #SP_BADCTRLERR      ;Bad Status Code Error ($21)    
                sta errorno
                rts
                        
;-------------------------------                        
; Smartport Host Status
;
; Action: Return a 8 bytes Status list at address pointed by Status List Pointer
; The first byte is no. of drive and the remaining bytes are zero.
;
; return SP_BUSERR($06) if device is offline
;
sp_status_u0_sc0:
                ;Fill in the zero from offset 0-7
                ldy #7                  ;Status List Length - 1
                lda #0
:               jsr putstatus
                dey
                bpl :-
                
                ;
                ;Is Device connected?
                ;
                jsr isonline
                bcs sp_devoffline

                ;
                ; Number of Units
                ;
                jsr getdevstatus        ;Acc = Unit Count, Carry set if error
                bcs sp_devoffline

                ;Store Unit Count to Status List
                ldy #0
                jsr putstatus
                        
                ;Success!
                stz errorno
                rts

sp_devoffline:  lda #SP_BUSERR
                sta errorno
                rts
                

;------------------------------------------
;
;Get Status of one unit
;Input: X=Status Code
;
sp_status_unit:
                ;Dispatch by Status Code in X
                txa                     ;update z-flag
                beq sp_status_sc0
                dex
                beq sp_status_sc1
                dex
                beq sp_status_sc2
                dex
                beq sp_status_sc3
                
                ;Invalid status code
                lda #SP_BADCTRLERR
                sta errorno
known_rts2:     rts

;----------------------------------------------
;Status Code 2 Handler
;
                ;For Char Device
                ;Do nothing and use Default Action
                ;i.e. return SP_BADCMDERR
sp_status_sc2   := known_rts2  ;Point it to the nearest rts to save memory

;----------------------------------------------
;Status Code 0 Handler
;                       
;Status Code 0 return DSB (4 Bytes)
;The DSB is same as the first 4 bytes of DIB 
;
sp_status_sc0:  stz errorno             ;Assume no error

                ;Validate Unit Num
                jsr sp_chkunitnumsc     ;No return if error                     

                ;Get DSB
                jsr getdsb
sc0chkerr:      bcc @noerr
                lda #SP_BUSERR
                sta errorno
@noerr:         rts
              

;----------------------------------------------
;Status Code 3 Handler
;                       
;Status Code 3 return DIB (25 Bytes)
;
sp_status_sc3:  stz errorno             ;Assume No Error
                
                ;Validate Unit Num
                jsr sp_chkunitnumsc     ;No return if error                     

                ;Get DIB
                jsr getdib
                bra sc0chkerr           ;re-use the code
                
                        
;Device Information Block
;Device Status Byte:
;Bit 7: 1=block device, 0=char Device
;Bit 6: 1=write allowed
;Bit 5: 1=read allowed
;Bit 4: 1=Device Online or Disk in Drive
;Bit 3: 1=Format allowed
;Bit 2: 1=media write-protected (block device only)
;Bit 1: Reserved, must = 0
;Bit 0: 1=device currently open (char device only)

.if 0   ;Sample DIB Table for reference
dib_table:      .byte %11111000         ;Device Status
                .byte 0                 ;Block Size Low Byte
                .byte 0                 ;Block Size Middle Byte
                .byte 0                 ;Block Size High Byte
                .byte IDSTRLEN          ;ID String Length ($10 is maximum)
                lascii IDSTR            ;ID String Padded to 16 bytes
                .byte $02               ;Device Type $02= harddisk, $00=RAM Disk, $04=ROM Disk
                .byte $20               ;Deivce Subtype = not removable, no extended
                .word SPDRIVERVERSION   ;Firmware Version                       

                ;Make sure the lenght of DIB is correct
                .assert *-dib_table = 25, error, "DIB Table Length not 25"
.endif



                        
;----------------------------------------------
;Status Code 1 Handler
;
;return a 1 byte DCB (can't return 0 byte)
sp_status_sc1:  stz errorno     ;no error

                lda #1
                ldy #0
                jsr putstatus   ;Write 1(len) to offset 0

                dec a           ;a=0                    
                iny             ;y=1
                jmp putstatus   ;Write 0(Data) to offset 1
                                ;jsr + rts



;----------------------------------------------------
;Validate Block Number
;return if block number is valid
;Otherwise, it sets up error no. and return to exit routine
;
; Input Block Size in AXY, Block Number in spBlockNum24
;       A - Low Byte
;       X - Mid Byte
;       Y - High Byte
;
sp_chkblocknum:
                ;Block Number is valid if Block Size > Block Number
                ;Test: Block Size - Block Number -1 >= 0
                ; i.e. AXY - Block Number -1 >=0
                                        ;A = Low Byte of Block Size
                clc                     ;minus one
                sbc spBlockNum24
                txa                     ;A = Mid Byte of Block Size
                sbc spBlockNum24+1
                tya                     ;A = High Byte of Block Size
                sbc spBlockNum24+2
                bge goodblock

badblock:       lda #SP_BADBLKERR
                sta errorno

                ;Pull the return address out from stack
                ;to return to exit routine
                pla
                pla

goodblock:      rts                

        

;--------------------------------------------------------------
;Validate Unit Num
;return if unit number is valid
;Otherwise, it sets up error no. and return to exit routine
;
;Note: According to Technical Reference, Status call should
;report BusErr ($06) only. While ReadBlock/WriteBlock/format
;call can report NoDrive($28) error.
;So, there are two entry points. sp_chkunitnumsc for Status call.
;sp_chkunitnum for ReadBlock/WriteBlock/Format
;
sp_chkunitnumsc: 
                ldx #SP_BUSERR          ;Report SP_BUSERR if error
                skip2                   ;skip ldx #SP_NODRVERR
sp_chkunitnum:  ldx #SP_NODRVERR        ;Report SP_NODRVERR if error
                
                ;spUnitNum = 0?
                lda spUnitNum
                beq badun               ;0 is not a valid unitNum

                ;Unit Count should be >= spUnitNume
                phx                     ;Save X
                jsr getdevstatus        ;Acc = Number of Unit
                plx                     ;Restore X
                cmp spUnitNum           ;Unit Count >= spUnitNum?
                bge unitnum_ok
                        
badun:          ;Invalid Unit Number Error
                stx errorno
                
                ;Pull the return address out from stack
                ;to return to exit routine
                pla
                pla
                        
unitnum_ok:     rts


;----------------------------------------------
; Get Unit Status
;
; Input: spUnitNum
;
; Input Block Size (Number of Blocks) in AXY
;       A - Low Byte
;       X - Mid Byte
;       Y - High Byte
;
; If no error, return block size in AXY
; If error, setup error no and don't return to 
; exit routine directly.
; According to IIgs Firmware Reference, we can
; only return BusError($06) or BadCtl($21) error.
; So, we return BusError if there is any error
;
sp_getunitstatus:
                jsr getunitstatus
                bcc :+                  ;Branch if no error
                lda #SP_BUSERR          ;Communication Error
                sta errorno
                ;Pull the return address out from stack
                ;to return to exit routine
                pla
                pla
:               rts


;*********************************************************
; Smartport ReadBlock Command handler
;
sp_readblock:   ;validate unit number
                jsr sp_chkunitnum       ;No return if error

                ;Check Unit status
                ;Return block size in AXY
                jsr sp_getunitstatus    ;No return if error

                ;validate block number
                jsr sp_chkblocknum      ;No return if error

                ;Read the block
                jsr readblock
                
                ;According to IIgs Techincal Notes#25
                ;On return X(Low Byte) and Y(High Byte) registers indicate the Number
                ;of bytes transfered.            
                
                ;Assume error. set XY to $0
                stz xval
                stz yval
                bcs :+                  ;Branch if error
                
                ;No error, set return value of XY to $100 (512 bytes)
                inc yval                ;change it to 1
:               rts
                        
;*********************************************************
; Smartport WriteBlock Command handler
;
sp_writeblock:
                ;validate unit number
                jsr sp_chkunitnum       ;No return if error

                ;Check Unit status
                ;Return block size in AXY
                jsr sp_getunitstatus    ;No return if error

                ;validate block number
                jsr sp_chkblocknum      ;No return if error

                jmp writeblock
                        
;*********************************************************
; Smartport Control Command Handler
;
sp_control:     ;X = Control Code
                ldx spControlCode
                
                ;Default to Bad Control Code error
                lda #SP_BADCTRLERR      ;Bad Control Code                       
                
                ;Validate Control Code
                ;should < 5
                cpx #5
                bge :+                  ;Branch if >=5

                ;Good Control Code
                lda ctrl_result_table,x ;Get the response from the table
:               sta errorno
                rts

ctrl_result_table:
                .byte SP_NOERR          ;Control code 0 = Reset
                .byte SP_NOERR          ;Control code 1 = SetDCB
                .byte SP_BADCTRLERR     ;Control code 2 = SetNewline
                .byte SP_NOINTRPT       ;Control code 3 = ServiceInterrupt
                .byte SP_NOERR          ;Control code 4 = Eject                 
                
;*********************************************************
; Smartport Format Command Handler              
;
; Check the device status and validate unit number
;
; When Copy II+ 8.4/9.1 format a drive, it performs SP_FORMAT call
; and then read block 2 of the unit.
;
; Block 2 is ProDOS Volume Directory Header. Offset $29-$2A is the
; block count of the volume. Copy II+ uses that value as the size of
; volume. It fails to use SP_STATUS call to get the size of the volume.
;
; To work around the problem, we may intercept the SP_FORMAT call. Then,
; write the block size to $29-$2A of block 2 so that Copy II+ can format
; correctly. Format call is supposed to destory existing data. 
; So, this workaround should be safe to other applications.
; 
sp_format:      jsr sp_chkunitnum       ;no return if error
                jsr sp_getunitstatus    ;no return if error
                
                jsr writeblocksizetovdh ;Device Driver Call
                                        ;Workaround of Copy II+ Format Bug

                ;fall-into sp_init

;*********************************************************
; Smartport Init Command Handler                
;
sp_init:        stz errorno             ;Always Success for Init Call
                rts                     

;**************************************************************** 
; #     #                                        
; ##   ##  ######  #    #   ####   #####   #   # 
; # # # #  #       ##  ##  #    #  #    #   # #  
; #  #  #  #####   # ## #  #    #  #    #    #   
; #     #  #       #    #  #    #  #####     #   
; #     #  #       #    #  #    #  #   #     #   
; #     #  ######  #    #   ####   #    #    #   
;****************************************************************          

;-----------------------------------------------------------------------
; Parameter List and Status List may be in Language Card Area
; We can't access them when the code is in ROM area ($D000-$FFFF).
; These routines resides in $Cn page (IOROM segment. They restore 
; the LC setting and read/write the lists. Then switch back to ROM.

;-------------------------------------------------------------------          
; Copy parameter list to spParamList
; The first byte of parameter list is not copied. It is returned
; in accumlator.
; So, parameter list offset #1 is copied to spParamList offset #0
; and so on.
;
; Input: spParamListPtr = pointer to parameter list
;
; Output: A = Parameter Count (Offset #0 of Paramter List)
;
; Note: To save memory, spParamListPtr is actually at the
;       the first two bytes of spParamList. So, special 
;       handling is needed. 
;       spParamListPtr is destroyed when return
;
                .segment "IOROM"
                .reloc
copyparam:      ldx lcstate
                inc $c000,x     ;Restore LC Status

                ldy #SPPARAMLEN-1
:               lda (spParamListPtr),y
                sta spParamList-1,y
                dey
                cpy #2
                bne :-
                
                ;now y=2
                ;spParamListPtr is actualy at spParamList and spParamList+1
                ;Writing to those address destroys the pointer.
                ;So, read parameter offset 0-2 to registers first
                ;Then, write to destination
                lda (spParamListPtr),y  ;Read Parameter List offset #2
                tax                     ;Save it to x
		dey			;y=1
                lda (spParamListPtr),y  ;Read Parameter List offset #1
                tay			;Save it to y
		lda (spParamListPtr)	;Read Parameter List offset #0 (Parameter Count)
		sty spParamList-1+1     ;Store Parameter List offset #1
                stx spParamList-1+2     ;Store Parameter List offset #2
                
                sta romain              ;Switch back to ROM ($D000-$FFFF)
                rts                     




;-------------------------
; Read ParamList
; Input: Y = offset, spParamListPtr = pointer to Parameter List
; Output: Acc = Parameter List value
;
; Note: X is destroyed.
;
                .segment "IOROM"
                .reloc
getparam:       ldx lcstate
                inc $c000,x             ;Restore LC
                
                lda (spParamListPtr),y  
                sta romain              ;Switch back to ROM ($D000-$FFFF)
                rts     
                        
;-------------------------
; Write to Status List
; Input: Y = offset, Acc = Value to be written
;        spStatusListPtr = pointer to Status List
;
; Note: X is destroyed.
;
                .segment "IOROM"
                .reloc
putstatus:      ldx lcstate
                inc $c000,x             ;Restore LC
                
                sta (spStatusListPtr),y
                sta romain              ;Switch back to ROM ($D000-$FFFF)
                rts



;**************************************************************** 
; ######                                      
; #     #  ######  #####   #    #   ####      
; #     #  #       #    #  #    #  #    #     
; #     #  #####   #####   #    #  #          
; #     #  #       #    #  #    #  #  ###     
; #     #  #       #    #  #    #  #    #     
; ######   ######  #####    ####    ####      
;**************************************************************** 

                .segment "DEBUG"
                .reloc
;-------------------------------------------------------------
; Debug Routine: Print ProDOS parameters
;
.if DEBUG
prnpdparam:     ;Print the call parameters
                ;       
                ;Printout 'P' to indicate ProDOS call
                lda #'P'
                jsr print
                
                ;Command Char
                ldx pdCommandCode
                lda pd_cmdchar_table,x
                jsr print
                jsr printspc
                
                ;Unit Number
                lda #'U'
                jsr print
                lda pdUnitNumber
                jsr printaspc
                
                ;Buffer Address
                lda #'A'
                jsr print                       
                lda pdIOBufferH
                ldy pdIOBuffer
                jsr printayspc

                ;Block Number
                lda #'B'
                jsr print       
                lda pdBlockNumH
                ldy pdBlockNum
                jmp printayspc
                
pd_cmdchar_table:
                .byte 'S','R','W','F'
.endif

;-------------------------------------------------------------
; Debug Routine: Print Smartport parameters
; Input: A = Parameter Count
;
                .segment "DEBUG"
                .reloc
.if DEBUG
prnspparam:     pha                 ;Save Parameter Count Byte

                lda #'S'            ;indicate Smartport Call
                jsr print

                ;Show Command Char
                ldx spCommand
                phx             ;Save it to stack                
                lda sp_cmdchar_table,x
                jsr print
                jsr printspc
                
                ;Get Parameter Length to X
                ply             ;y = spCommand
                ldx sp_plen_table,y

                ;Print Parameter Count Byte
                pla
                jsr printaspc

                dex             ;Parameter Count is in A. So number of bytes is reduced by 1
                ldy #0
:               lda spParamList,y
                jsr printaspc
                iny
                dex
                bne :-
                rts
                        
;Parameters Length of each command
sp_plen_table:
                .byte 5,7,7,2,5,2,2,2,9,9
                
;Command abbreviation. e.g S for Status, R for Read                
sp_cmdchar_table:
                .byte 'S','R','W','F','C','I'
.endif

;-------------------------------------------------------------
; Debug Routine: Print Result Code (errorno)
; Acc destroyed
;
                .segment "DEBUG"
                .reloc
.if DEBUG
prnerr:         lda #':'            ;indicate Result
                jsr print
                
                lda errorno
                jsr printaspc
                
                lda #13         ;CR
                jsr print
                lda #10         ;LF
to_print:       jmp print

;
; Print A:Y as 16-bit hex and then a space char
;
printayspc:     jsr printa
                tya
                ;fall into printaspc
;
;Print Acc as hex and then a space char
;
printaspc:      jsr printa
                ;fall into printspc
;
;Print a space char
;
printspc:       lda #32 ;Space
                bra to_print

;
;Print Acc as hex
;
printa:         pha     ;Save A to stack
                lsr
                lsr
                lsr
                lsr
                jsr printnibble
                pla     ;Restore it
                and #$0F
                ;fall-into printnibble

printnibble:    
                ora #$30        ;Add $30
                cmp #$3A        ;<$3A?  i.e. '0'-'9'
                bcc to_print    ;Yes, print it out
                adc #6          ;Carry is set. Add 7 to print A-F
                bra to_print

.endif
