dsp_dump.bin: bl_stage3.S dsp_dump.lds
	c54x-coff-as bl_stage3.S -o bl_stage3.o
	c54x-coff-ld --script dsp_dump.lds bl_stage3.o -o dsp_dump.coff
	c54x-coff-objcopy -j .text -O binary dsp_dump.coff dsp_dump.bin

clean:
	rm -f *.o *.bin *.coff
