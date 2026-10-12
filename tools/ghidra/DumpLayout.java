// Postscript: writes image base, memory blocks and symbol sources of the
// imported program as JSON, so tools/ghidra/import_kernel.py can compare
// them with the PE headers.
// Args: <output file>
// @category Cleanroom
import java.io.PrintWriter;
import java.util.Map;
import java.util.TreeMap;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Symbol;

public class DumpLayout extends GhidraScript {
	@Override
	protected void run() throws Exception {
		Map<String, Integer> sources = new TreeMap<>();
		// Every symbol not named by Ghidra's defaults goes to a side file, so
		// names that do not come from the binary can be found and checked.
		try (PrintWriter named = new PrintWriter(getScriptArgs()[0] + ".symbols.txt")) {
			for (Symbol s : currentProgram.getSymbolTable().getAllSymbols(true)) {
				sources.merge(s.getSource().toString(), 1, Integer::sum);
				if (s.getSource() != ghidra.program.model.symbol.SourceType.DEFAULT) {
					named.printf("%s %s %s%n", s.getAddress(), s.getSource(), s.getName(true));
				}
			}
		}
		try (PrintWriter out = new PrintWriter(getScriptArgs()[0])) {
			out.println("{");
			out.printf("  \"image_base\": \"0x%x\",%n", currentProgram.getImageBase().getOffset());
			out.printf("  \"language\": \"%s\",%n", currentProgram.getLanguageID());
			out.printf("  \"functions\": %d,%n", currentProgram.getFunctionManager().getFunctionCount());
			out.print("  \"symbol_sources\": {");
			String sep = "";
			for (Map.Entry<String, Integer> e : sources.entrySet()) {
				out.printf("%s\"%s\": %d", sep, e.getKey(), e.getValue());
				sep = ", ";
			}
			out.println("},");
			out.println("  \"blocks\": [");
			MemoryBlock[] blocks = currentProgram.getMemory().getBlocks();
			for (int i = 0; i < blocks.length; i++) {
				MemoryBlock b = blocks[i];
				out.printf("    {\"name\": \"%s\", \"start\": \"0x%x\", \"size\": \"0x%x\", \"perm\": \"%s%s%s\", \"initialized\": %b}%s%n",
					b.getName(), b.getStart().getOffset(), b.getSize(),
					b.isRead() ? "r" : "-", b.isWrite() ? "w" : "-", b.isExecute() ? "x" : "-",
					b.isInitialized(), i + 1 < blocks.length ? "," : "");
			}
			out.println("  ]");
			out.println("}");
		}
	}
}
