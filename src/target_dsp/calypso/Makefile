all: dsp_dump.bin dsp_sniff.bin

CROSS=tic54x-coff-

%.o: %.S
	$(CROSS)as $< -o $@

%.bin: %.coff
	$(CROSS)objcopy -j .text -O binary $< $@

dsp_dump.coff: bl_stage3.o dsp_dump.lds
	$(CROSS)ld --script dsp_dump.lds bl_stage3.o -o $@

dsp_sniff.coff: dsp_sniff.o dsp_patch.lds
	$(CROSS)ld --script dsp_patch.lds dsp_sniff.o -o $@

clean:
	rm -f *.o *.bin *.coff
