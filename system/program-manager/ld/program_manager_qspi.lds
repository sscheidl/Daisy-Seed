/* Daisy Program Manager: BOOT_QSPI application that keeps the SRAM image target free.
 *
 * Derived from libDaisy's STM32H750IB_qspi.lds / STM32H750IB_sram.lds (BSD, see those
 * files). Differences, see docs/CONTRACT.md section 5:
 *   - code, rodata, vectors and init arrays execute in place from QSPI at 0x90040000,
 *     the bootloader's standard application slot; the image must end below
 *     0x907E0000 (Mod on Daisy storage reservation, modules/mod-on-daisy/docs/STORAGE.md);
 *   - .data lives in DTCM with its load address in QSPI, .bss and the stack in DTCM
 *     (like a BOOT_SRAM application), the heap in D2 SRAM;
 *   - 0x24000000..0x24078000 (SRAM_TARGET) receives the loaded program and holds no
 *     section of this image; tools/check_qspi_image.py verifies that from the ELF;
 *   - the upper 32 KiB of AXI SRAM (SRAM_HELPER) hold the SDMMC1 IDMA bounce buffer
 *     (.pm_axi_bounce), which DTCM cannot provide.
 */

ENTRY(Reset_Handler)

MEMORY
{
	FLASH       (RX)  : ORIGIN = 0x08000000, LENGTH = 128K
	DTCMRAM     (RWX) : ORIGIN = 0x20000000, LENGTH = 128K
	SRAM_TARGET (RW)  : ORIGIN = 0x24000000, LENGTH = 480K
	SRAM_HELPER (RWX) : ORIGIN = 0x24078000, LENGTH = 32K
	RAM_D2_DMA  (RWX) : ORIGIN = 0x30000000, LENGTH = 32K
	RAM_D2      (RWX) : ORIGIN = 0x30008000, LENGTH = 256K
	RAM_D3      (RWX) : ORIGIN = 0x38000000, LENGTH = 64K
	BACKUP_SRAM (RWX) : ORIGIN = 0x38800000, LENGTH = 4K
	ITCMRAM     (RWX) : ORIGIN = 0x00000000, LENGTH = 64K
	SDRAM       (RWX) : ORIGIN = 0xc0000000, LENGTH = 64M
	QSPIFLASH   (RX)  : ORIGIN = 0x90040000, LENGTH = 0x7A0000
}

_estack = 0x20020000;

SECTIONS
{
	.isr_vector :
	{
		. = ALIGN(4);
		KEEP(*(.isr_vector))
		. = ALIGN(4);
	} > QSPIFLASH

	.text :
	{
		. = ALIGN(4);
		_stext = .;

		*(.text)
		*(.text*)
		*(.rodata)
		*(.rodata*)
		*(.glue_7)
		*(.glue_7t)
		KEEP(*(.init))
		KEEP(*(.fini))
		. = ALIGN(4);
		_etext = .;

	} > QSPIFLASH

	.ARM.extab :
	{
		. = ALIGN(4);
		*(.ARM.extab)
		*(.gnu.linkonce.armextab.*)
		. = ALIGN(4);
	} > QSPIFLASH

	.exidx :
	{
		. = ALIGN(4);
		PROVIDE(__exidx_start = .);
		*(.ARM.exidx*)
		. = ALIGN(4);
		PROVIDE(__exidx_end = .);
	} > QSPIFLASH

	.ARM.attributes 0 : { *(.ARM.attributes) }

	.preinit_array :
	{
		PROVIDE_HIDDEN(__preinit_array_start = .);
		KEEP(*(.preinit_array*))
		PROVIDE_HIDDEN(__preinit_array_end = .);
	} > QSPIFLASH

	.init_array :
	{
		PROVIDE_HIDDEN(__init_array_start = .);
		KEEP(*(SORT(.init_array.*)))
		KEEP(*(.init_array*))
		PROVIDE_HIDDEN(__init_array_end = .);
	} > QSPIFLASH

	.fini_array :
	{
		PROVIDE_HIDDEN(__fini_array_start = .);
		KEEP(*(.fini_array*))
		KEEP(*(SORT(.fini_array.*)))
		PROVIDE_HIDDEN(__fini_array_end = .);
	} > QSPIFLASH

	/* libDaisy DMA buffers (DMA_BUFFER_MEM_SECTION): D2 SRAM, MPU non-cacheable. */
	.sram1_bss (NOLOAD) :
	{
		. = ALIGN(4);
		_ssram1_bss = .;

		PROVIDE(__sram1_bss_start__ = _sram1_bss);
		*(.sram1_bss)
		*(.sram1_bss*)
		. = ALIGN(4);
		_esram1_bss = .;

		PROVIDE(__sram1_bss_end__ = _esram1_bss);
	} > RAM_D2_DMA

	/* Initialized data: copied by Reset_Handler from QSPI (memory mapped by the
	   bootloader before the jump) into DTCM. */
	.data :
	{
		. = ALIGN(4);
		_sdata = .;

		PROVIDE(__data_start__ = _sdata);
		*(.data)
		*(.data*)
		. = ALIGN(4);
		_edata = .;

		PROVIDE(__data_end__ = _edata);
	} > DTCMRAM AT > QSPIFLASH

	_sidata = LOADADDR(.data);

	.bss (NOLOAD) :
	{
		. = ALIGN(4);
		_sbss = .;

		PROVIDE(__bss_start__ = _sbss);
		*(.bss)
		*(.bss*)
		*(COMMON)
		. = ALIGN(4);
		_ebss = .;

		PROVIDE(__bss_end__ = _ebss);
	} > DTCMRAM

	.dtcmram_bss (NOLOAD) :
	{
		. = ALIGN(4);
		_sdtcmram_bss = .;

		PROVIDE(__dtcmram_bss_start__ = _sdtcmram_bss);
		*(.dtcmram_bss)
		*(.dtcmram_bss*)
		. = ALIGN(4);
		_edtcmram_bss = .;

		PROVIDE(__dtcmram_bss_end__ = _edtcmram_bss);
	} > DTCMRAM

	/* SDMMC1 IDMA bounce buffer and nothing else: AXI SRAM above the image target. */
	.pm_axi_bounce (NOLOAD) :
	{
		. = ALIGN(32);
		__pm_axi_bounce_start = .;
		KEEP(*(.pm_axi_bounce))
		KEEP(*(.pm_axi_bounce*))
		. = ALIGN(32);
		__pm_axi_bounce_end = .;
	} > SRAM_HELPER

	.sdram_bss (NOLOAD) :
	{
		. = ALIGN(4);
		_ssdram_bss = .;

		PROVIDE(__sdram_bss_start = _ssdram_bss);
		*(.sdram_bss)
		*(.sdram_bss*)
		. = ALIGN(4);
		_esdram_bss = .;

		PROVIDE(__sdram_bss_end = _esdram_bss);
	} > SDRAM

	/* libDaisy's boot_info (12 bytes at 0x38800000): the bootloader's JUMP order. */
	.backup_sram (NOLOAD) :
	{
		. = ALIGN(4);
		_sbackup_sram = .;

		PROVIDE(__backup_sram_start = _sbackup_sram);
		*(.backup_sram)
		*(.backup_sram*)
		. = ALIGN(4);
		_ebackup_sram = .;

		PROVIDE(__backup_sram_end = _ebackup_sram);
	} > BACKUP_SRAM

	/* QSPI data sections of a BOOT_SRAM application make no sense here: they would
	   be writes into the chip this code executes from. Keep them empty. */
	.qspiflash_text : { *(.qspiflash_text) *(.qspiflash_text*) } > QSPIFLASH
	.qspiflash_data : { *(.qspiflash_data) *(.qspiflash_data*) } > QSPIFLASH
	.qspiflash_bss (NOLOAD) : { *(.qspiflash_bss) *(.qspiflash_bss*) } > QSPIFLASH
	ASSERT(SIZEOF(.qspiflash_text) == 0, "qspiflash_text must be empty in the program manager")
	ASSERT(SIZEOF(.qspiflash_data) == 0, "qspiflash_data must be empty in the program manager")
	ASSERT(SIZEOF(.qspiflash_bss) == 0, "qspiflash_bss must be empty in the program manager")

	.heap (NOLOAD) :
	{
		. = ALIGN(4);
		PROVIDE(__heap_start__ = .);
		PROVIDE(__heap_start = .);
		KEEP(*(.heap))
		. = ALIGN(4);
		PROVIDE(__heap_end = .);
		PROVIDE(__heap_end__ = .);
	} > RAM_D2

	PROVIDE(end = .);

	.reserved_for_stack (NOLOAD) :
	{
		. = ALIGN(4);
		PROVIDE(__reserved_for_stack_start__ = .);
		KEEP(*(.reserved_for_stack))
		. = ALIGN(4);
		PROVIDE(__reserved_for_stack_end__ = .);
	} > DTCMRAM

	/* The image must stay below the Mod on Daisy storage blocks. */
	ASSERT(LOADADDR(.data) + SIZEOF(.data) <= ORIGIN(QSPIFLASH) + LENGTH(QSPIFLASH), "QSPI image too large")
	/* Stack reserve in DTCM above .bss and the per-app stack reservation. */
	ASSERT(__reserved_for_stack_end__ + 16K <= _estack, "Less than 16 KiB of DTCM stack left")

    DISCARD :
    {
        libc.a ( * )
        libm.a ( * )
        libgcc.a ( * )
    }

}
