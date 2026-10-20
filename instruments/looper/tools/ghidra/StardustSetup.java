// Ghidra pre-analysis script for the Stardust v1.1.1 raw image.
// Analysis only: it maps memory and marks entry points; it does not modify the input file.
//
// Import with BinaryLoader at base 0x24000000, language ARM:LE:32:v8-m (the Cortex variant
// uses the ARMv7 SLEIGH spec and does not decode the FPv5 instructions VSEL, VMAXNM,
// VMINNM, VRINT, VCVTA/N/P/M that the Cortex-M7 build uses), then run this
// script as -preScript. It adds:
//   * .data   0x24044000, 1,044 bytes, initialized from image offset 0x35C40 (startup copy)
//   * .bss    0x24044420 .. 0x24070838 (zeroed by startup)
//   * rest of AXI SRAM, DTCM, D2/D3 SRAM, and SDRAM 0xC0000000 (64 MiB), uninitialized
//   * Thumb mode for the whole image and functions at every vector table entry
//@category Stardust

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;

import java.math.BigInteger;

public class StardustSetup extends GhidraScript {

    private static final long BASE = 0x24000000L;
    private static final long DATA_SRC = 0x24035C40L;
    private static final long DATA_DST = 0x24044000L;
    private static final int DATA_LEN = 1044;

    private void uninit(String name, long start, long len, boolean exec) throws Exception {
        Memory mem = currentProgram.getMemory();
        MemoryBlock b = mem.createUninitializedBlock(name, toAddr(start), len, false);
        b.setRead(true);
        b.setWrite(true);
        b.setExecute(exec);
    }

    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        MemoryBlock image = mem.getBlock(toAddr(BASE));
        image.setName("image");
        // Code and constants only (.data and .bss are separate blocks); read-only lets the
        // decompiler fold literal-pool constants.
        image.setWrite(false);
        long imageEnd = image.getEnd().getOffset() + 1;

        byte[] data = new byte[DATA_LEN];
        mem.getBytes(toAddr(DATA_SRC), data);
        MemoryBlock d = mem.createInitializedBlock(".data", toAddr(DATA_DST),
            new java.io.ByteArrayInputStream(data), DATA_LEN, monitor, false);
        d.setRead(true);
        d.setWrite(true);

        uninit(".bss", 0x24044420L, 0x24070838L - 0x24044420L, false);
        uninit("axisram_rest", 0x24070838L, 0x24080000L - 0x24070838L, false);
        uninit("axisram_gap", imageEnd, DATA_DST - imageEnd, false);
        uninit("dtcm", 0x20000000L, 0x20000L, false);
        uninit("itcm", 0x00000000L, 0x10000L, true);
        uninit("sram_d2", 0x30000000L, 0x48000L, false);
        uninit("sram_d3", 0x38000000L, 0x10000L, false);
        uninit("backup", 0x38800000L, 0x1000L, false);
        uninit("periph", 0x40000000L, 0x20000000L, false);
        uninit("qspi", 0x90000000L, 0x800000L, false);
        uninit("sdram", 0xC0000000L, 0x4000000L, false);
        uninit("ppb", 0xE0000000L, 0x100000L, false);

        Register tmode = currentProgram.getProgramContext().getRegister("TMode");
        currentProgram.getProgramContext().setValue(tmode, image.getStart(), image.getEnd(),
            BigInteger.ONE);

        // Vector table: SP, then handlers. Stop at the first word that is not a Thumb
        // pointer into the image (and not zero).
        int created = 0;
        for (int i = 1; i < 240; i++) {
            long v = mem.getInt(toAddr(BASE + 4L * i)) & 0xFFFFFFFFL;
            if (v == 0) {
                continue;
            }
            if ((v & 1) == 0 || v < BASE || v >= imageEnd) {
                println("vector table ends at entry " + i);
                break;
            }
            Address a = toAddr(v & ~1L);
            createDWord(toAddr(BASE + 4L * i));
            if (getFunctionAt(a) == null) {
                createFunction(a, i == 1 ? "Reset_Handler" : null);
                created++;
            }
        }
        addEntryPoint(toAddr(0x24001894L));
        createLabel(toAddr(0x24016008L), "main", true, SourceType.USER_DEFINED);
        println("vector functions created: " + created);
    }
}
