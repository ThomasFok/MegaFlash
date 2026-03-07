.PHONY: all a2cp a2c clean

all: a2cp a2c 
#Toolchain
CA65=ca65
LD65=ld65
CL65=cl65

#Disable Implicit Rules
%.o:%.c	     


#### Common Source Files ####
SRCCOMMON = slotrom.s smartport.s dispatch.s
BOOTMENUFILE=bootmenu.s
FPUFILE=fpu.s
PATCHFILE=patches.s
ACCELFILE=accel.s
INC = defines.inc macros.inc buildflags.inc ../common/defines.inc

#### Apple IIc Plus ####
FLAGS_IICP= $(FLAGS) -D IICP
MACHINE_IICP=iicplus
DEVICEDRIVER_IICP=megaflash.s
MERGEFILE_IICP=merge_iicp.s

##### Apple IIc #####
FLAGS_IIC= $(FLAGS) -D IIC
MACHINE_IIC=iic
DEVICEDRIVER_IIC=megaflash.s
MERGEFILE_IIC=merge_iic.s



###################### A2CP ######################

SRC_IICP = $(SRCCOMMON) $(DEVICEDRIVER_IICP) $(PATCHFILE) $(BOOTMENUFILE) $(ACCELFILE) $(FPUFILE)
OBJ_IICP = $(addprefix $(INTOUTDIR_IICP)/,$(SRC_IICP:.s=.o))
INTOUTDIR_IICP = $(MACHINE_IICP)
MERGECFG_IICP = $(MERGEFILE_IICP:.s=.cfg)

a2cp: iicplus.bin

#Build b?_????.bin fragments and merge into rom file
iicplus.bin: $(OBJ_IICP) $(MACHINE_IICP).cfg $(MERGECFG_IICP) $(MERGEFILE_IICP) $(INC) Makefile
	$(LD65) --config $(MACHINE_IICP).cfg -Ln $(INTOUTDIR_IICP)/$(MACHINE_IICP).txt -m $(INTOUTDIR_IICP)/$(MACHINE_IICP).map $(OBJ_IICP)
	$(CL65) --config $(MERGECFG_IICP) $(FLAGS_IICP) $(MERGEFILE_IICP)
	@rm -f b?_????.bin
	@rm -f a.out

#Assemble .s to .o
$(INTOUTDIR_IICP)/%.o: %.s $(INC) | $(INTOUTDIR_IICP)
	$(CA65) -g -l $(@D)/$*.lst $(FLAGS_IICP) $*.s -o $(@D)/$*.o



###################### A2C ######################
SRC_IIC = $(SRCCOMMON) $(DEVICEDRIVER_IIC) $(PATCHFILE) $(BOOTMENUFILE) $(FPUFILE)
OBJ_IIC = $(addprefix $(INTOUTDIR_IIC)/,$(SRC_IIC:.s=.o))
INTOUTDIR_IIC = $(MACHINE_IIC)
MERGECFG_IIC = $(MERGEFILE_IIC:.s=.cfg)

a2c: iic.bin

#Build b?_????.bin fragments and merge into rom file
iic.bin: $(OBJ_IIC) $(MACHINE_IIC).cfg $(MERGECFG_IIC) $(MERGEFILE_IIC) $(INC) Makefile
	$(LD65) --config $(MACHINE_IIC).cfg -Ln $(INTOUTDIR_IIC)/$(MACHINE_IIC).txt -m $(INTOUTDIR_IIC)/$(MACHINE_IIC).map $(OBJ_IIC)
	$(CL65) --config $(MERGECFG_IIC) $(FLAGS_IIC) $(MERGEFILE_IIC)
	@rm -f b?_????.bin
	@rm -f a.out

#Assemble .s to .o
$(INTOUTDIR_IIC)/%.o: %.s $(INC) | $(INTOUTDIR_IIC)
	$(CA65) -g -l $(@D)/$*.lst $(FLAGS_IIC) $*.s -o $(@D)/$*.o



###################### Sub-Directory ######################
$(INTOUTDIR_IICP) $(INTOUTDIR_IIC):
	mkdir -p $@
	
	
###################### CLEAN ######################	
clean:
	@# Intermediate Files
	rm -f iicplus/*.o iicplus/*.lst iicplus/*.map iicplus/iicplus.txt
	rm -f iic/*.o iic/*.lst iic/*.map iic/iic.txt
	
	@# Output Files
	rm -f iic.bin
	rm -f iicplus.bin

