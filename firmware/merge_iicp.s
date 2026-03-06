;--------------------------------------------------------
; Apple IIc MegaFlash Firmware
; Module: Apple IIC+ ROM Patching
; Version: 0.1
; Date: 28-Sep-2023
; This file along with merge_iicp.cfg is passed to cl65
; to merge all generated fragments (b?_????.bin) into
; the original ROM file (rom5.bin).

        ;Bank 0 Original Content
        .segment "B0_ORG"        
        .incbin "rom5.bin",0,$4000
        
        ;Bank 1 Original Content
        .segment "B1_ORG"        
        .incbin "rom5.bin",$4000,$4000
		
        ;
        ;MegaFlash Firmware
        ;
        .segment "B0_C400"              ;Slot ROM Segment
        .incbin "b0_c400.bin"
        
        .segment "B1_C58E"              ;IOROM Segment
        .incbin "b1_c58e.bin"
        
        .segment "B1_C755"              ;slxeq hook
        .incbin "b1_c755.bin"
        
        .segment "B1_D800"
        .incbin "b1_d800.bin"
        
        .segment "B1_F800"
        .incbin "b1_f800.bin"
				
        .segment "B1_FEC0"
        .incbin "b1_fec0.bin"
        
        .segment "B1_DB63"
        .incbin "b1_db63.bin"

        .segment "B1_D516"
        .incbin "b1_d516.bin"

        .segment "B1_D6CE"
        .incbin "b1_d6ce.bin"
        
        .segment "B1_FD00"              ;IIc+ Accelerator Code
        .incbin "b1_fd00.bin"
				
        ;
        ;ROM Patches
        ;
        .segment "B0_C755"              ;BellFix, Old Wait Routine
        .incbin "b0_c755.bin"
        
        .segment "B0_FBDD"              ;Bell Fix
        .incbin "b0_fbdd.bin"
    
	.segment "B0_FB68"		;Center Title Fix
	.incbin "b0_fb68.bin"    
    
        .segment "B1_C53D"		;Remove ROM Checksum test
        .incbin "b1_c53d.bin"    
        
        .segment "B0_DB6C"              ;IIc+ Slow PRINT Speed Fix
        .incbin "b0_db6c.bin"           
    
        .segment "B0_FB19"              ;Coldstart
        .incbin "b0_fb19.bin"
				
	.segment "B0_FAC8"		;BootMenu Entry
	.incbin "b0_fac8.bin"
        
        ;
        ;Applesoft
        ;        
        .segment "B0_C1DB"              ;Various Bug Fixes 
        .incbin "b0_c1db.bin"        
        
        .segment "B0_F315"              ;ONERR GOTO Fix
        .incbin "b0_f315.bin"        
        
        .segment "B0_E112"              ;INT Fix
        .incbin "b0_e112.bin"        
        
        .segment "B0_C7FC"              ;ROM Switch at $C7FC
        .incbin "b0_c7fc.bin"
        
        .segment "B1_C7FF"              ;ROM Switch at $C7FC
        .incbin "b1_c7ff.bin"
				
        .segment "B0_E7C6"              ;FADD
        .incbin "b0_e7c6.bin"
		
        .segment "B0_E987"              ;FMUL
        .incbin "b0_e987.bin"
        
        .segment "B0_EA6B"              ;FDIV
        .incbin "b0_ea6b.bin"
		
        .segment "B0_EFF1"              ;FSIN
        .incbin "b0_eff1.bin"
        
        .segment "B0_EFEA"              ;FCOS
        .incbin "b0_efea.bin"        
        
        .segment "B0_F03A"              ;FTAN
        .incbin "b0_f03a.bin"
        
        .segment "B0_F09E"              ;FATN
        .incbin "b0_f09e.bin"
        
        .segment "B0_EF09"              ;FEXP
        .incbin "b0_ef09.bin"

        .segment "B0_E94B"              ;FLOG
        .incbin "b0_e94b.bin"
        
        .segment "B0_ED36"              ;FOUT
        .incbin "b0_ed36.bin"
        
