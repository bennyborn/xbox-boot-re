// Postscript: writes every function of the program with its body, the
// calls it makes and the memory it references as JSON, for
// tools/ghidra/export_graph.py. No decompiler output, only addresses.
// Args: <output file> [entry address ...]   extra entries are disassembled
// and made functions first (code reached only through pointers).
// @category Cleanroom
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.FlowType;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;

public class ExportGraph extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		boolean seeded = false;
		for (int i = 1; i < args.length; i++) {
			Address a = toAddr(Long.decode(args[i]));
			if (getFunctionAt(a) == null) {
				disassemble(a);
				createFunction(a, null);
				seeded = true;
			}
		}
		if (seeded) {
			analyzeChanges(currentProgram);
		}
		try (PrintWriter out = new PrintWriter(args[0])) {
			out.println("[");
			String sep = "";
			for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
				StringBuilder b = new StringBuilder();
				b.append(sep).append("{\"entry\":\"").append(f.getEntryPoint())
					.append("\",\"name\":\"").append(f.getName())
					.append("\",\"source\":\"").append(f.getSymbol().getSource())
					.append("\",\"body\":[");
				String s2 = "";
				for (AddressRange r : f.getBody()) {
					b.append(s2).append("[\"").append(r.getMinAddress()).append("\",\"")
						.append(r.getMaxAddress()).append("\"]");
					s2 = ",";
				}
				b.append("],\"calls\":[");
				StringBuilder data = new StringBuilder();
				String s3 = "", s4 = "";
				InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
				while (it.hasNext()) {
					Instruction ins = it.next();
					FlowType ft = ins.getFlowType();
					for (Reference r : ins.getReferencesFrom()) {
						RefType t = r.getReferenceType();
						if (t.isCall()) {
							b.append(s3).append("[\"").append(ins.getAddress()).append("\",\"")
								.append(r.getToAddress()).append("\"]");
							s3 = ",";
						} else if (t.isData()) {
							data.append(s4).append("[\"").append(ins.getAddress()).append("\",\"")
								.append(r.getToAddress()).append("\",\"").append(t.getName()).append("\"]");
							s4 = ",";
						}
					}
					if (ft.isCall() && ft.isComputed()) {
						b.append(s3).append("[\"").append(ins.getAddress()).append("\",\"indirect\"]");
						s3 = ",";
					}
				}
				b.append("],\"data\":[").append(data).append("]}");
				out.println(b);
				sep = ",";
			}
			out.println("]");
		}
	}
}
