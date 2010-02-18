OUTPUT_FORMAT("coff1-c54x")
OUTPUT_ARCH("")
MEMORY
{
	apiram (RWXI) : ORIGIN = 0x0800, LENGTH = 0x2000
}
SECTIONS
{
	. = 0x0800;

	.apiram :
	{
		PROVIDE(_api_ram = .);
		*(.apiram)
	} > apiram

	.text :
	{
		*(.text)
	} > apiram
}

