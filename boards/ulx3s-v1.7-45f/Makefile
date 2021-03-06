
PROJ_FILE := $(shell ls *.ldf | head -1)
PROJ_NAME := $(shell fgrep default_implementation ${PROJ_FILE} | cut -d'"' -f 4)
IMPL_NAME := $(shell fgrep default_implementation ${PROJ_FILE} | cut -d'"' -f 8)
IMPL_DIR := $(shell fgrep default_strategy ${PROJ_FILE} | cut -d'"' -f 4)

DIAMOND_BASE := /usr/local/diamond
DIAMOND_BIN :=  $(shell find ${DIAMOND_BASE}/ -maxdepth 2 -name bin | sort -rn | head -1)
DIAMONDC := $(shell find ${DIAMOND_BIN}/ -name diamondc)
DDTCMD := $(shell find ${DIAMOND_BIN}/ -name ddtcmd)

OPENOCD_BASE := ../../programmer/openocd/ulx3s/

# name of the project as defined in project file
PROJECT = project

# FPGA flashing device for programming
FPGA_DEVICE = LFE5U-45F

JUNK = ${IMPL_DIR} .recovery ._Real_._Math_.vhd *.sty reportview.xml
JUNK += dummy_sym.sort project_tcl.html promote.xml
JUNK += generate_core.tcl generate_ngd.tcl msg_file.log
JUNK += project_tcr.dir

all: $(PROJECT)/$(PROJECT)_$(PROJECT).bit \
	$(PROJECT)/$(PROJECT)_$(PROJECT)_sram.svf \
	$(PROJECT)/$(PROJECT)_$(PROJECT)_sram.vme \
	$(PROJECT)/$(PROJECT)_$(PROJECT)_multiboot_flash_micron_32mbit.vme \
	$(PROJECT)/$(PROJECT)_$(PROJECT)_multiboot_flash_micron_32mbit.svf \
	$(PROJECT)/$(PROJECT)_$(PROJECT)_multiboot_flash_spansion_64mbit.vme \
	$(PROJECT)/$(PROJECT)_$(PROJECT)_multiboot_flash_spansion_64mbit.svf

$(PROJECT)/$(PROJECT)_$(PROJECT).bit:
	echo prj_project open ${PROJ_FILE} \; prj_run Export -task Bitgen | ${DIAMONDC}

# same file with different name required for multiboot to work
$(PROJECT)/$(PROJECT)_$(PROJECT)_altfile.bit: $(PROJECT)/$(PROJECT)_$(PROJECT).bit
	cp $< $@

$(PROJECT)/$(PROJECT)_$(PROJECT)_sram.vme: $(PROJECT)/$(PROJECT)_$(PROJECT).bit
	LANG=C ${DDTCMD} -oft -fullvme -if ulx3s_45f_sram.xcf -nocompress -noheader -of $@

$(PROJECT)/$(PROJECT)_$(PROJECT)_sram.svf: $(PROJECT)/$(PROJECT)_$(PROJECT).bit
	LANG=C ${DDTCMD} -oft -svfsingle -revd -maxdata 8 -if ulx3s_45f_sram.xcf -of $@

$(PROJECT)/$(PROJECT)_$(PROJECT).mcs: $(PROJECT)/$(PROJECT)_$(PROJECT).bit
	LANG=C ${DDTCMD} -dev $(FPGA_DEVICE) -if $(PROJECT)/$(PROJECT)_$(PROJECT).bit -oft -int -of $@

$(PROJECT)/$(PROJECT)_$(PROJECT)_flash_micron_32mbit.vme: $(PROJECT)/$(PROJECT)_$(PROJECT).mcs
	LANG=C ${DDTCMD} -oft -fullvme -if ulx3s_45f_flash_micron_32mbit.xcf -nocompress -noheader -of $@

$(PROJECT)/$(PROJECT)_$(PROJECT)_flash_spansion_64mbit.vme: $(PROJECT)/$(PROJECT)_$(PROJECT).mcs
	LANG=C ${DDTCMD} -oft -fullvme -if ulx3s_45f_flash_spansion_64mbit.xcf -nocompress -noheader -of $@

$(PROJECT)/$(PROJECT)_$(PROJECT)_multiboot_flash_micron_32mbit.mcs: $(PROJECT)/$(PROJECT)_$(PROJECT).bit $(PROJECT)/$(PROJECT)_$(PROJECT)_altfile.bit
	LANG=C ${DDTCMD} -dev $(FPGA_DEVICE) -oft -advanced -format int -flashsize 32 -header  \
	-if $(PROJECT)/$(PROJECT)_$(PROJECT).bit \
	-golden $(PROJECT)/$(PROJECT)_$(PROJECT).bit \
	-multi 1 -altfile $(PROJECT)/$(PROJECT)_$(PROJECT)_altfile.bit -address 0x200000 -next prim -of $@

$(PROJECT)/$(PROJECT)_$(PROJECT)_multiboot_flash_micron_32mbit.vme: $(PROJECT)/$(PROJECT)_$(PROJECT)_multiboot_flash_micron_32mbit.mcs
	LANG=C ${DDTCMD} -oft -fullvme -if ulx3s_45f_multiboot_micron_32mbit.xcf -nocompress -noheader -of $@

$(PROJECT)/$(PROJECT)_$(PROJECT)_multiboot_flash_micron_32mbit.svf: $(PROJECT)/$(PROJECT)_$(PROJECT)_multiboot_flash_micron_32mbit.mcs
	LANG=C ${DDTCMD} -oft -svfsingle -revd -maxdata 8 -if ulx3s_45f_multiboot_micron_32mbit.xcf -of $@

$(PROJECT)/$(PROJECT)_$(PROJECT)_multiboot_flash_spansion_64mbit.mcs: $(PROJECT)/$(PROJECT)_$(PROJECT).bit $(PROJECT)/$(PROJECT)_$(PROJECT)_altfile.bit
	dd if=/dev/zero of=/tmp/zero.bit bs=1k count=300
	LANG=C ${DDTCMD} -dev $(FPGA_DEVICE) -oft -advanced -format int -flashsize 64 -header  \
	-if $(PROJECT)/$(PROJECT)_$(PROJECT).bit \
	-golden $(PROJECT)/$(PROJECT)_$(PROJECT).bit \
	-multi 1 -altfile $(PROJECT)/$(PROJECT)_$(PROJECT)_altfile.bit -address 0x200000 -next prim -of $@

$(PROJECT)/$(PROJECT)_$(PROJECT)_multiboot_flash_spansion_64mbit.vme: $(PROJECT)/$(PROJECT)_$(PROJECT)_multiboot_flash_spansion_64mbit.mcs
	LANG=C ${DDTCMD} -oft -fullvme -if ulx3s_45f_multiboot_spansion_64mbit.xcf -nocompress -noheader -of $@

$(PROJECT)/$(PROJECT)_$(PROJECT)_multiboot_flash_spansion_64mbit.svf: $(PROJECT)/$(PROJECT)_$(PROJECT)_multiboot_flash_spansion_64mbit.mcs
	LANG=C ${DDTCMD} -oft -svfsingle -revd -maxdata 8 -if ulx3s_45f_multiboot_spansion_64mbit.xcf -of $@

program: $(PROJECT)/$(PROJECT)_$(PROJECT).bit
	echo pgr_project open ulx3s_45f_sram.xcf \; pgr_program run | ${DIAMONDC}

program_wifi: $(PROJECT)/$(PROJECT)_$(PROJECT)_sram.svf
	openocd --file=$(OPENOCD_BASE)/remote.ocd --file=$(OPENOCD_BASE)/ecp5-45f.ocd

program_web: $(PROJECT)/$(PROJECT)_$(PROJECT)_sram.svf
	svfupload.py ulx3s.lan $<

program_web_flash: $(PROJECT)/$(PROJECT)_$(PROJECT)_multiboot_flash_micron_32mbit.svf
	svfupload.py ulx3s.lan $<

program_ft2232: $(PROJECT)/$(PROJECT)_$(PROJECT)_sram.svf
	openocd --file=$(OPENOCD_BASE)/ft2232-fpu1.ocd --file=$(OPENOCD_BASE)/ecp5-45f.ocd

program_flea: $(PROJECT)/$(PROJECT)_$(PROJECT)_sram.vme
	FleaFPGA-JTAG $(PROJECT)/$(PROJECT)_$(PROJECT)_sram.vme

program_flea_flash: $(PROJECT)/$(PROJECT)_$(PROJECT)_multiboot_flash_micron_32mbit.vme
	FleaFPGA-JTAG $<

program_flea_flash_spansion: $(PROJECT)/$(PROJECT)_$(PROJECT)_multiboot_flash_spansion_64mbit.vme
	FleaFPGA-JTAG $<

#$(PROJECT)/$(PROJECT)_$(PROJECT).jed:
#	echo prj_project open ${PROJ_FILE} \; prj_run Export -task Jedecgen | ${DIAMONDC}

#$(PROJECT)/$(PROJECT)_$(PROJECT)_flash.vme: $(PROJECT)/$(PROJECT)_$(PROJECT).jed
#	${DDTCMD} -oft -fullvme -if sparrowhawk_flash_micron_32mbit.xcf -nocompress -noheader -of $@

#$(PROJECT)/$(PROJECT)_$(PROJECT)_flash.svf: $(PROJECT)/$(PROJECT)_$(PROJECT).jed
#	${DDTCMD} -oft -svfsingle -op "FLASH Erase,Program,Verify" -if $< -of $@

flash: $(PROJECT)/$(PROJECT)_$(PROJECT)_flash.vme
	${PROGRAMMERC} $<
	# after this, to gain access to serial port on linux
	# rmmod ftdi_sio; modprobe ftdi_sio

# example another project
#%.svf : %.jed
#	${DDTCMD} -oft -svfsingle -op "FLASH Erase,Program,Verify" -if $< -of $@
#	mv -f $@ $@.flash
#	${DDTCMD} -oft -svfsingle -revd -op "SRAM Fast Program" -if $< -of $@
#	mv -f $@ $@.sram
#	./svf_to_urjtag.pl <$@.flash | sed 's/,/./g' > $@

clean:
	rm -rf $(JUNK) *~
