// Postscript: applies the names of docs/decomp/README.md to the program,
// for tools/ghidra/apply_names.py, and reports what it did.
// Args: <rows file> <report file> <range start> <range end>
// Rows file: one row per line, "<address> <f|d> <name>". f makes a function
// at the address first if there is none (code reached only through
// pointers), d puts a label on data. The report lists every row with the
// function found at its address, and every function in [start, end) that
// has no row.
// @category Cleanroom
import java.io.BufferedReader;
import java.io.FileReader;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;

public class ApplyNames extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		List<String[]> rows = new ArrayList<>();
		try (BufferedReader in = new BufferedReader(new FileReader(args[0]))) {
			String line;
			while ((line = in.readLine()) != null) {
				if (!line.isBlank()) {
					rows.add(line.trim().split(" "));
				}
			}
		}
		boolean seeded = false;
		for (String[] r : rows) {
			Address a = toAddr(Long.decode(r[0]));
			if (r[1].equals("f") && getFunctionAt(a) == null) {
				disassemble(a);
				createFunction(a, null);
				seeded = true;
			}
		}
		if (seeded) {
			analyzeChanges(currentProgram);
		}
		Set<Address> named = new HashSet<>();
		try (PrintWriter out = new PrintWriter(args[1])) {
			for (String[] r : rows) {
				Address a = toAddr(Long.decode(r[0]));
				named.add(a);
				if (r[1].equals("f")) {
					Function f = getFunctionAt(a);
					if (f == null) {
						out.printf("row %s f %s nofunction%n", a, r[2]);
						continue;
					}
					f.setName(r[2], SourceType.USER_DEFINED);
					out.printf("row %s f %s ok%n", a, f.getName());
				} else {
					Symbol s = createLabel(a, r[2], true, SourceType.USER_DEFINED);
					out.printf("row %s d %s %s%n", a, r[2], s == null ? "nolabel" : "ok");
				}
			}
			Address lo = toAddr(Long.decode(args[2]));
			Address hi = toAddr(Long.decode(args[3]));
			for (Function f : currentProgram.getFunctionManager().getFunctions(lo, true)) {
				if (f.getEntryPoint().compareTo(hi) >= 0) {
					break;
				}
				if (!named.contains(f.getEntryPoint())) {
					out.printf("unnamed %s %s%n", f.getEntryPoint(), f.getName());
				}
			}
		}
	}
}
