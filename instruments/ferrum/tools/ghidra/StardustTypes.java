// Ghidra script: declares opaque object types for the looper, its tape/head object, and the
// effect chain, and applies them to the entry points documented in STARDUST_ANALYSIS.md, so
// the decompiler prints fields as field_0xNN with their access width instead of reinterpreting
// integers as floats. Field names are not assigned here; the analysis document is the map.
//@category Stardust

import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;

public class StardustTypes extends GhidraScript {

    private DataTypeManager dtm;

    private DataType struct(String name, int size) {
        StructureDataType s = new StructureDataType(name, size);
        return dtm.addDataType(s, DataTypeConflictHandler.REPLACE_HANDLER);
    }

    private void sig(long entry, String name, DataType ret, Object... params) {
        FunctionDefinitionDataType f = new FunctionDefinitionDataType(name);
        f.setReturnType(ret);
        ParameterDefinition[] p = new ParameterDefinition[params.length / 2];
        for (int i = 0; i < p.length; i++) {
            p[i] = new ParameterDefinitionImpl((String) params[2 * i], (DataType) params[2 * i + 1], null);
        }
        f.setArguments(p);
        ApplyFunctionSignatureCmd cmd =
            new ApplyFunctionSignatureCmd(toAddr(entry), f, SourceType.USER_DEFINED);
        if (!cmd.applyTo(currentProgram, monitor)) {
            println("signature failed at " + Long.toHexString(entry) + ": " + cmd.getStatusMsg());
        }
    }

    @Override
    public void run() throws Exception {
        dtm = currentProgram.getDataTypeManager();
        DataType looper = struct("Looper", 0x208b0);
        DataType tape = struct("Tape", 0x314);
        DataType fx = struct("Fx", 0x204e4);
        DataType lp = new PointerDataType(looper);
        DataType tp = new PointerDataType(tape);
        DataType fp = new PointerDataType(fx);
        DataType f = FloatDataType.dataType;
        DataType pf = new PointerDataType(f);
        DataType ppf = new PointerDataType(pf);
        DataType u = UnsignedIntegerDataType.dataType;
        DataType b = ByteDataType.dataType;
        DataType v = VoidDataType.dataType;

        sig(0x24010190L, "looper_process", v, "lp", lp, "in", ppf, "out", ppf, "n", u);
        sig(0x2400f808L, "looper_init", v, "sr", f, "lp", lp, "sdram", pf, "size", u);
        sig(0x2400bec4L, "tape_process", f, "inL", f, "inR", f, "t", tp, "outL", pf, "outR", pf);
        sig(0x2400b454L, "tape_head", u, "inL", f, "inR", f, "a", f, "b", f, "t", tp, "flag", b);
        sig(0x2400dbb0L, "fx_process", v, "inL", f, "inR", f, "fx", fp, "outL", pf, "outR", pf);

        clearListing(toAddr(0x24047280L), toAddr(0x24047280L + 0x208b0 - 1));
        createData(toAddr(0x24047280L), looper);
        createLabel(toAddr(0x24047280L), "g_looper", true, SourceType.USER_DEFINED);
        println("types applied");
    }
}
