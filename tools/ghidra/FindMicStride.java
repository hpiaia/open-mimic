// Find code using the 0x158-byte mic-record stride in the Editor project model.
//@category OpenMimic

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;
import java.util.LinkedHashMap;
import java.util.Map;

public class FindMicStride extends GhidraScript {
    @Override
    public void run() throws Exception {
        Map<Function,Integer> functions = new LinkedHashMap<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Instruction ins = it.next();
            boolean hit = false;
            for (int i = 0; i < ins.getNumOperands() && !hit; i++) {
                for (Object obj : ins.getOpObjects(i)) {
                    if (obj instanceof Scalar) {
                        Scalar scalar = (Scalar)obj;
                        if (scalar.getSignedValue() == 0x158 || scalar.getUnsignedValue() == 0x158) {
                            hit = true;
                            break;
                        }
                    }
                }
            }
            if (hit) {
                Function fn = getFunctionContaining(ins.getAddress());
                if (fn != null) {
                    functions.put(fn, functions.getOrDefault(fn, 0) + 1);
                    println(ins.getAddress() + " " + fn.getName() + " @ " +
                        fn.getEntryPoint() + " :: " + ins);
                }
            }
        }
        println("TOTAL_FUNCTIONS=" + functions.size());
        for (Map.Entry<Function,Integer> e : functions.entrySet()) {
            println("FUNCTION " + e.getKey().getEntryPoint() + " " +
                e.getKey().getName() + " hits=" + e.getValue());
        }
    }
}
