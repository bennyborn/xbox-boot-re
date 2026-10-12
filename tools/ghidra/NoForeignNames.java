// Prescript for every import of this line: turns off the analyzers that
// bring names or types from outside the binary (signature databases, type
// archives, symbol servers, the Windows user-mode TEB model, lists of known
// function names). Names in this line come from strings in the binary or
// describe what is seen. All analyzer settings are written to the log.
// @category Cleanroom
import java.util.Map;
import java.util.Set;

import ghidra.app.script.GhidraScript;

public class NoForeignNames extends GhidraScript {
	private static final Set<String> OFF = Set.of(
		"Function ID",
		"Apply Data Archives",
		"PDB Universal",
		"PDB MSDIA",
		"Windows x86 Thread Environment Block (TEB) Analyzer",
		"Non-Returning Functions - Known");

	@Override
	protected void run() throws Exception {
		Map<String, String> opts = getCurrentAnalysisOptionsAndValues(currentProgram);
		// An analyzer missing for this format cannot run either. The callers
		// check the log for "analyzer: Function ID = false".
		for (String name : OFF) {
			if (opts.containsKey(name)) {
				setAnalysisOption(currentProgram, name, "false");
			}
		}
		opts = getCurrentAnalysisOptionsAndValues(currentProgram);
		for (String name : opts.keySet()) {
			if (!name.contains(".")) {
				println("analyzer: " + name + " = " + opts.get(name));
			}
		}
	}
}
