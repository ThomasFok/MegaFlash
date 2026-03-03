;
; This files is shared with firmware and cpanel projects  
;


;
;Peripheral Registers
;
cmdreg          := $c0c0        ;Command Register
statusreg       := $c0c0        ;Status Register
paramreg        := $c0c1        ;Parameter Register
datareg         := $c0c2        ;Data Register
idreg           := $c0c3        ;ID Register

;
;MegaFlash Command/Constant
;
CMD_RESETBOTHPTRS       =       $00
CMD_RESETDATAPTR        =       $01
CMD_RESETPARAMPTR       =       $02
CMD_MODELINEAR          =       $03
CMD_MODEINTERLEAVED     =       $04
CMD_COLDSTART           =       $05
CMD_ENABLEROMDISK       =       $06
CMD_DISABLEROMDISK      =       $07
CMD_LOAD_CPANEL         =       $08
CMD_TESTWIFI            =       $09
CMD_GETINFOSTR          =       $0A

CMD_GETDEVINFO          =       $10
CMD_GETDEVSTATUS        =       $11
CMD_GETUNITSTATUS       =       $12
CMD_GETDIB              =       $13
CMD_GETVOLINFO          =       $14
CMD_READBLOCK           =       $15
CMD_WRITEBLOCK          =       $16
CMD_GETPRODOSTIME       =       $17
CMD_GETPRODOS25TIME     =       $18
CMD_GETTIMESTR          =       $19
CMD_SETRTC_PRODOS       =       $1A
CMD_SETRTC_PRODOS25     =       $1B
CMD_WRITEBLOCKSIZETOVDH =       $1C
CMD_FORMATDISK          =       $1D
CMD_ERASEDISK           =       $1E   
     
CMD_SAVEUSERSETTINGS    =       $20
CMD_GETUSERSETTINGS     =       $21
CMD_SAVEWIFISETTINGS    =       $22
CMD_GETCONFIGBYTES      =       $23
CMD_ERASEUSERSETTINGS   =       $24
CMD_ERASEWIFISETTINGS   =       $25       
CMD_ERASEADVSETTINGS    =       $26 
CMD_ERASEALLSETTINGS    =       $27  
CMD_DRIVEMAPPING        =       $28
        
CMD_FADD                =       $30 
CMD_FMUL                =       $31
CMD_FDIV                =       $32
CMD_FSIN                =       $33 
CMD_FCOS                =       $34
CMD_FTAN                =       $35
CMD_FATN                =       $36
CMD_FLOG                =       $37
CMD_FEXP                =       $38
CMD_FSQR                =       $39
CMD_FOUT                =       $3A
CMD_FSUB								=				$3B

CMD_RESETSTOPWATCH_US   =       $40
CMD_GETSTOPWATCH_US     =       $41
CMD_RESETSTOPWATCH_MS   =       $42
CMD_GETSTOPWATCH_MS     =       $43
CMD_RESETSTOPWATCH_S    =       $44
CMD_GETSTOPWATCH_S      =       $45

CMD_TFTPRUN             =       $50
CMD_TFTPSTATUS          =       $51
CMD_TFTPGETLASTSERVER   =       $52
CMD_TFTPSAVELASTSERVER  =       $53


WE_KEY                  =       $71     ;Write Enable Key
SIGNATURE1              =       $88     ;MegaFlash Device Signature Byte #1
SIGNATURE2              =       $74     ;MegaFlash Device Signature Byte #2

;
;Status Registers Fields
;
BUSYFLAG                = %10000000
ERRORFLAG               = %01000000
ERRORCODEFIELD          = %00011111 ;Last five bits are error code field


;
;MegaFlash Activation Magic Address Sequence
;
MAGIC1                  =       $C0C2
MAGIC2                  =       $C0C0
MAGIC3                  =       $C0C0
MAGIC4                  =       $C0C3
MAGIC5                  =       $C0C1

;
; Bits definition of configbyte1
;
CPUSPEEDFLAG            =       %10000000
AUTOBOOTFLAG            =       %01000000
RAMDISKFLAG             =       %00100000
NTPCLIENTFLAG           =       %00010000
FPUFLAG                 =       %00001000

;
;FPU Support Signature
;ROM Bank 0 $C7FF is set to this value if FPU support is enabled in firmware
;
FPU_SUPPORT_SIGNATURE   =     $6B