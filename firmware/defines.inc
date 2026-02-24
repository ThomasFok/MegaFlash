;
; Constants
;
SPDRIVERVERSION   =       $0000              ;SmartPort Version, reported to host 
FWVERSION         =       $02                ;Version of this Firmware, Stored at $C4FA
.if DEBUG
.define TITLESTR "MegaFlash Firmware D1.0.1"   ;Shown on boot menu screen, Debug Version
.else
.define TITLESTR "MegaFlash Firmware V1.0.1"   ;Shown on boot menu screen
.endif
; $01 = Version 1.0    13-Jul-2025
; $02 = Version 1.0.1 

.define IDSTR   "MEGAFLASH DRV N "           ;ID String Padded to 16 bytes
IDSTRLEN          =       15
IDSTR_DN_OFFSET   =       14                 ;Offset to Drive Number Char

;On IIC+, if booting from slot 4 fails, try to boot from slot 5
.ifdef IICP
NEXTBOOTSLOT    =     $C5             ;IIc+: $C5
.endif

;On IIc, try slot 6
.ifdef IIC
NEXTBOOTSLOT    =     $C6             ;IIc : $C6
.endif

;
;Device Signature Bytes
;There are two device signature bytes in slotrom ($Cn00-$CnFF) to identify MegaFlash
; $Cn10 = 77 (ASCII code of 'M')
; $CnFA = 70 (ASCII code of 'F')
;
;
SIGNATURE_AT_10   = 77          ;$4D
SIGNATURE_AT_FA   = 70          ;$46


;
;Boot Code/Boot Menu
;
BCRUN             =     $0200   ;Address of Boot code in RAM
BMRUN             =     $0800   ;Address of Boot Menu in RAM
CPANELADDR        =     $0A00   ;Address of Control Panel in RAM

;
;Firmware Routine ID
;
MODE_PRODOS       :=      $00
MODE_SP           :=      MODE_PRODOS|$80 ;MSB set to indicate Smartport call
MODE_COPYBC       :=      $1              ;Copy Bootcode to RAM
MODE_INIT         :=      $2              ;Cold Start Init
MODE_CLOCKDRV     :=      $3              ;ProDOS clockdriver
MODE_LOADCPANEL   :=      $4              ;Load Control Panel code to CPANELADDR
MODE_LOADBOOTMENU :=      $5              ;Load BootMenu code to BMRUN
                                          ;Currently Not Used by the firmware. 
                                          ;Provide a well-defined method to execute bootmenu


;
; Apple II I/O Soft Switches
;
kbd             :=      $C000   ;R last key pressed
set80storeoff   :=      $C000   ;W Turn off 80STORE
set80storeon    :=      $C001   ;W Turn on 80STORE
rdmainram       :=      $C002   ;W If 80STORE Off: Read Main Mem $0200-$BFFF
rdcardram       :=      $C003   ;W If 80STORE Off: Read Aux Mem $0200-$BFFF
wrmainram       :=      $C004   ;W If 80STORE Off: Write Main Mem $0200-$BFFF
wrcardram       :=      $C005   ;W If 80STORE Off: Write Aux Mem $0200-$BFFF
setslotcxrom    :=      $C006   ;W Peripheral  ROM ($C100-$CFFF)
setincxrom      :=      $C007   ;W Internal ROM ($C100-$CFFF)
setstdzp        :=      $C008   ;W Main Stack and Zero Page
setaltzp        :=      $C009   ;W Aux Stack and Zero Page
setintc3rom     :=      $C00A   ;W Use Internal Slot 3 ROM
setslotc3rom    :=      $C00B   ;W Use Card Slot 3 ROM
kbdstrb         :=      $C010   ;W clear keyboard strobe
rdlcbnk2        :=      $C011   ;R7 Status of LC Bank 2
rdlcram         :=      $C012   ;R7 Status of LC RAM Status
rdramrd         :=      $C013   ;R7 Status of Main/Aux RAM Reading
rdramwrt        :=      $C014   ;R7 Status of Main/Aux RAM Writing
rdcxrom         :=      $C015   ;R7 Status of Peripheral/Internal ROM
rdaltzp         :=      $C016   ;R7 Status of Main/Aux Stack and Zero Page
rdc3rom         :=      $C017   ;R7 Status of Slot 3 Int/Slot ROM
rd80store       :=      $C018   ;R7 Status of 80STORE
rombank         :=      $C028   ;W IIc ROM Bank Switch
spkr            :=      $C030   ;R Toggle Speaker
butn0           :=      $C061   ;R7 Open Apple Key/Joystick Button 0
butn1           :=      $C062   ;R7 Closed Apple Key/Joystick Button 1
romain          :=      $C081   ;W Switch to ROM ($D000-$FFFF)
acia1data       :=      $C098   ;  ACIA 1 Data Register
acia1status     :=      $C099   ;  ACIA 1 Status Register
acia1cmd        :=      $C09A   ;  ACIA 1 Command Register
acia1ctrl       :=      $C09B   ;  ACIA 1 Control Register
acia2data       :=      $C0A8   ;  ACIA 2 Data Register
acia2status     :=      $C0A9   ;  ACIA 2 Status Register
acia2cmd        :=      $C0AA   ;  ACIA 2 Command Register
acia2ctrl       :=      $C0AB   ;  ACIA 2 Control Register

;
; ProDOS Block Device Driver
;
pdCommandCode   :=      $42
pdUnitNumber    :=      $43
pdIOBuffer      :=      $44
pdIOBufferH     :=      $45
pdBlockNum      :=      $46
pdBlockNumH     :=      $47
;ProDOS command
PD_STATUS       =       0
PD_READ         =       1
PD_WRITE        =       2
PD_FORMAT       =       3
;ProDOS error
PD_NOERR        =       $0
PD_IOERR        =       $27
PD_NODEVERR     =       $28
PD_WRPROTECTERR =       $2B

;
; Smartport Device Driver
;
;SmartPort command
SP_STATUS       =       $00
SP_READBLOCk    =       $01
SP_WRITEBLOCK   =       $02
SP_FORMAT       =       $03
SP_CONTROL      =       $04
SP_INIT         =       $05
SP_OPEN         =       $06
SP_CLOSE        =       $07
SP_READ         =       $08
SP_WRITE        =       $09

;Smartport Error Code
SP_NOERR        =       $00     ;No Error
SP_BADCMDERR    =       $01     ;Unimplemented Command
SP_BADPCNTERR   =       $04     ;Bad Call Parameter Count
SP_BUSERR       =       $06     ;Communication error
SP_NOINTRPT     =       $1F     ;No interrupt
SP_BADCTRLERR   =       $21     ;Invalid Status Code
SP_IOERR        =       $27     ;I/O Error
SP_NODRVERR     =       $28     ;No Device Connected
SP_NOWRITEERR   =       $2B     ;Write Protect Error
SP_BADBLKERR    =       $2D     ;Bad Block Number
SP_OFFLINEERR   =       $2F     ;Device Offline or No Disk

;Smartport DIB/DSB Length
DSB_LEN         =       4       ;Device Status Block Length
DIB_LEN         =       25      ;Device Information Block Length

;
; System ROM Routines
;
swrts           :=      $C784   ;Switch ROM Bank, then RTS
slxeq           :=      $C752   ;Aux Bank Routines Entry Point
getlc           :=      $C816   ;Bank 1, Get Language Card state
applesoft       :=      $E000
cr              :=      $FC62
cout            :=      $FDED
cout1           :=      $FDF0
prbyte          :=      $FDDA   ;Print Acc as Hexadecimal
setkbd          :=      $FE89
setvid          :=      $FE93
keyin           :=      $FD1B
bell            :=      $FBDD
appleii         :=      $FB60   ;Clear screen and show Apple IIc
vtab            :=      $FC22   ;Recalculate base address after changing cv
vtab23          :=      $FB59   ;Set Cursor Vertical Position to 23
wait            :=      $FCA8

;
; System Zero Page
;
ch              :=      $24     ;cursor horizontal position
cv              :=      $25     ;cursor vertical position
kswl            :=      $38     ;Input Hook Low Byte
kswh            :=      $39     ;Input Hook High Byte
himem           :=      $73     ;Applesoft HIMEM

