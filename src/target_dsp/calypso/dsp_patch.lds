OUTPUT_FORMAT("coff1-c54x")
OUTPUT_ARCH("")
MEMORY
{
	dram (RWXI)   : ORIGIN = 0x015C, LENGTH = 0x0600
	apiram (RWXI) : ORIGIN = 0x2000, LENGTH = 0x1000
}
SECTIONS
{
	. = 0x015C;

	.text :
	{
		*(.text)
	} > dram


	. = 0x2000;

	.apiram :
	{
		PROVIDE(_api_ram = .);
		*(.apiram)
	} > apiram
}
