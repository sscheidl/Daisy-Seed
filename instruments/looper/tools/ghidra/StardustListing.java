// Ghidra script: writes the disassembly of an address range for checking decompiler output.
// Output is proprietary-derived and must stay in instruments/looper/work/.
//
// Arguments: <outFile> <startHex> <endHex>
//@category Stardust

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

import java.io.File;
import java.io.PrintWriter;

public class StardustListing extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Address start = toAddr(Long.parseLong(args[1].replace("0x", ""), 16));
        Address end = toAddr(Long.parseLong(args[2].replace("0x", ""), 16));
        try (PrintWriter w = new PrintWriter(new File(args[0]))) {
            InstructionIterator it = currentProgram.getListing().getInstructions(start, true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                if (ins.getAddress().compareTo(end) > 0) {
                    break;
                }
                w.println(ins.getAddress() + "  " + ins);
            }
        }
    }
}
