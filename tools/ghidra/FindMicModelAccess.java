// Locate all code touching the instrument model's mic-count or mic-array range.
//@category OpenMimic

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;

import java.util.LinkedHashMap;
import java.util.Map;

public class FindMicModelAccess extends GhidraScript {
    private static final long MIC_COUNT = 0x80004L;
    private static final long MIC_ARRAY_START = 0x80008L;
    private static final long MIC_ARRAY_END = 0x80ac8L;

    @Override
    public void run() throws Exception {
        Map<Function, Integer> functions = new LinkedHashMap<>();
        InstructionIterator instructions = currentProgram.getListing().getInstructions(true);
        while (instructions.hasNext() && !monitor.isCancelled()) {
            Instruction ins = instructions.next();
            boolean matched = false;
            long value = 0;
            for (int i = 0; i < ins.getNumOperands() && !matched; i++) {
                for (Object obj : ins.getOpObjects(i)) {
                    if (obj instanceof Scalar) {
                        long signed = ((Scalar)obj).getSignedValue();
                        long unsigned = ((Scalar)obj).getUnsignedValue();
                        if (signed == MIC_COUNT || unsigned == MIC_COUNT ||
                            (signed >= MIC_ARRAY_START && signed < MIC_ARRAY_END) ||
                            (unsigned >= MIC_ARRAY_START && unsigned < MIC_ARRAY_END)) {
                            matched = true;
                            value = unsigned;
                            break;
                        }
                    }
                }
            }
            if (matched) {
                Function fn = getFunctionContaining(ins.getAddress());
                if (fn != null) {
                    functions.put(fn, functions.getOrDefault(fn, 0) + 1);
                    println(ins.getAddress() + " [0x" + Long.toHexString(value) + "] " +
                        fn.getName() + " @ " + fn.getEntryPoint() + " :: " + ins);
                }
            }
        }
        println("TOTAL_FUNCTIONS=" + functions.size());
        for (Map.Entry<Function, Integer> entry : functions.entrySet()) {
            Function fn = entry.getKey();
            println("FUNCTION " + fn.getEntryPoint() + " " + fn.getName() +
                " hits=" + entry.getValue());
        }
    }
}
