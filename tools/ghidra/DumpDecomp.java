// Local helper: seeds functions at the given entry addresses, re-runs
// analysis and writes the decompilation of every function to a file.
// The output stays in out/ and is never committed.
// Args: <output file> [entry address ...]
// @category Cleanroom
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DumpDecomp extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		for (int i = 1; i < args.length; i++) {
			Address a = toAddr(Long.decode(args[i]));
			disassemble(a);
			if (getFunctionAt(a) == null) {
				createFunction(a, null);
			}
		}
		analyzeAll(currentProgram);
		DecompInterface di = new DecompInterface();
		di.openProgram(currentProgram);
		try (PrintWriter out = new PrintWriter(args[0])) {
			for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
				DecompileResults r = di.decompileFunction(f, 60, monitor);
				out.println("// ==== " + f.getEntryPoint() + " " + f.getName());
				out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// failed");
			}
		}
	}
}
