// Ghidra post-analysis script: writes a function index and decompiled output for reading.
// Output is proprietary-derived and must stay in instruments/looper/work/.
//
// Arguments: <outDir> [hexAddr ...]
//   without addresses: funcs.tsv (entry, size, callers, callees) and all.c (every function)
//   with addresses:    one <addr>.c per containing function
//@category Stardust

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.File;
import java.io.PrintWriter;
import java.util.Set;
import java.util.TreeSet;

public class StardustDump extends GhidraScript {

    private DecompInterface dec;

    private String decompile(Function f) {
        DecompileResults r = dec.decompileFunction(f, 120, monitor);
        if (r == null || !r.decompileCompleted()) {
            return "/* decompile failed: " + (r == null ? "null" : r.getErrorMessage()) + " */\n";
        }
        return r.getDecompiledFunction().getC();
    }

    private String header(Function f) {
        Set<String> callers = new TreeSet<>();
        for (Function c : f.getCallingFunctions(monitor)) {
            callers.add(c.getEntryPoint().toString());
        }
        Set<String> callees = new TreeSet<>();
        for (Function c : f.getCalledFunctions(monitor)) {
            callees.add(c.getEntryPoint().toString());
        }
        return f.getEntryPoint() + "\t" + f.getName() + "\t" + f.getBody().getNumAddresses() +
            "\t" + String.join(",", callers) + "\t" + String.join(",", callees);
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args[0]);
        out.mkdirs();
        dec = new DecompInterface();
        DecompileOptions opt = new DecompileOptions();
        opt.grabFromProgram(currentProgram);
        dec.setOptions(opt);
        dec.openProgram(currentProgram);

        if (args.length > 1) {
            for (int i = 1; i < args.length; i++) {
                Function f = getFunctionContaining(toAddr(Long.parseLong(args[i].replace("0x", ""), 16)));
                if (f == null) {
                    println("no function at " + args[i]);
                    continue;
                }
                try (PrintWriter w = new PrintWriter(new File(out, f.getEntryPoint() + ".c"))) {
                    w.println("// " + header(f));
                    w.print(decompile(f));
                }
            }
            return;
        }

        try (PrintWriter idx = new PrintWriter(new File(out, "funcs.tsv"));
             PrintWriter all = new PrintWriter(new File(out, "all.c"))) {
            idx.println("entry\tname\tsize\tcallers\tcallees");
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            int n = 0;
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                String h = header(f);
                idx.println(h);
                all.println("// ==== " + h);
                all.println(decompile(f));
                n++;
            }
            println("functions dumped: " + n);
        }
    }
}
