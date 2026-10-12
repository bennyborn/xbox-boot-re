// Local helper: writes the decompilation of the functions that contain the
// given addresses, or of every function in [lo, hi) when an argument has the
// form lo-hi. Meant for a read-only run; the output stays in out/ and is
// never committed.
// Args: <output file> <address | lo-hi> ...
// @category Cleanroom
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DumpFuncs extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		Set<Function> funcs = new LinkedHashSet<>();
		for (int i = 1; i < args.length; i++) {
			String a = args[i];
			int dash = a.indexOf('-');
			if (dash > 0) {
				Address lo = toAddr(Long.decode(a.substring(0, dash)));
				Address hi = toAddr(Long.decode(a.substring(dash + 1)));
				for (Function f : currentProgram.getFunctionManager().getFunctions(lo, true)) {
					if (f.getEntryPoint().compareTo(hi) >= 0) {
						break;
					}
					funcs.add(f);
				}
			} else {
				Function f = getFunctionContaining(toAddr(Long.decode(a)));
				if (f == null) {
					println("no function at " + a);
				} else {
					funcs.add(f);
				}
			}
		}
		DecompInterface di = new DecompInterface();
		di.openProgram(currentProgram);
		try (PrintWriter out = new PrintWriter(args[0])) {
			for (Function f : funcs) {
				DecompileResults r = di.decompileFunction(f, 60, monitor);
				out.println("// ==== " + f.getEntryPoint() + " " + f.getName());
				if (r.decompileCompleted()) {
					out.println(r.getDecompiledFunction().getC());
				} else {
					out.println("// failed: " + r.getErrorMessage());
				}
			}
		}
	}
}
