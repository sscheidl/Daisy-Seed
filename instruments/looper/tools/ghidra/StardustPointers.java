// Ghidra script: creates functions at Thumb code pointers stored in the image and in .data
// (callbacks and vtables that auto-analysis does not reach), then re-runs analysis.
// A word qualifies if it is odd, points into the code part of the image, and the target is
// not inside an existing function.
//@category Stardust

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.util.TreeSet;

public class StardustPointers extends GhidraScript {

    private static final long CODE_START = 0x24000298L;
    private static final long CODE_END = 0x24034000L; // strings and tables follow

    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        TreeSet<Long> targets = new TreeSet<>();
        for (String name : new String[] {"image", ".data"}) {
            MemoryBlock b = mem.getBlock(name);
            long s = (b.getStart().getOffset() + 3) & ~3L;
            for (long a = s; a + 4 <= b.getEnd().getOffset() + 1; a += 4) {
                long v = mem.getInt(toAddr(a)) & 0xFFFFFFFFL;
                if ((v & 1) == 1 && v > CODE_START && v < CODE_END) {
                    targets.add(v & ~1L);
                }
            }
        }
        int made = 0;
        for (long t : targets) {
            Address a = toAddr(t);
            if (getFunctionContaining(a) != null) {
                continue;
            }
            if (getInstructionAt(a) == null) {
                disassemble(a);
            }
            if (getInstructionAt(a) != null && createFunction(a, null) != null) {
                made++;
            }
        }
        println("pointer candidates: " + targets.size() + ", functions created: " + made);
        analyzeChanges(currentProgram);
        println("functions now: " + currentProgram.getFunctionManager().getFunctionCount());
    }
}
