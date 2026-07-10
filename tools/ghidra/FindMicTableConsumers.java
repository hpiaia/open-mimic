// Find code that refers directly or indirectly to the Editor's instrument-format
// string table, with special attention to the contiguous mic-label region.
//@category OpenMimic

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.pcode.HighFunction;
import ghidra.program.model.pcode.PcodeOpAST;
import ghidra.program.model.pcode.Varnode;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.util.LinkedHashSet;
import java.util.Set;

public class FindMicTableConsumers extends GhidraScript {
    private static final long TABLE_START = 0x140522e00L;
    private static final long TABLE_END   = 0x140523500L;

    @Override
    public void run() throws Exception {
        Set<Function> hits = new LinkedHashSet<>();

        // References to any byte in the table, including references to its base.
        for (long value = TABLE_START; value < TABLE_END; value++) {
            Address target = toAddr(value);
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(target);
            while (refs.hasNext()) {
                Reference ref = refs.next();
                Function fn = getFunctionContaining(ref.getFromAddress());
                if (fn != null && hits.add(fn)) {
                    println("REF " + ref.getFromAddress() + " -> " + target +
                        " in " + fn.getName() + " @ " + fn.getEntryPoint());
                }
            }
        }

        // Operand scalars sometimes survive without a Reference object.
        InstructionIterator instructions = currentProgram.getListing().getInstructions(true);
        while (instructions.hasNext() && !monitor.isCancelled()) {
            Instruction ins = instructions.next();
            for (int i = 0; i < ins.getNumOperands(); i++) {
                for (Object obj : ins.getOpObjects(i)) {
                    if (obj instanceof Address) {
                        long value = ((Address)obj).getOffset();
                        if (value >= TABLE_START && value < TABLE_END) {
                            Function fn = getFunctionContaining(ins.getAddress());
                            if (fn != null && hits.add(fn)) {
                                println("OPERAND " + ins.getAddress() + " -> " + obj +
                                    " in " + fn.getName() + " @ " + fn.getEntryPoint());
                            }
                        }
                    }
                }
            }
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try {
            // P-code catches computed constants produced during decompilation.
            for (Function fn : currentProgram.getFunctionManager().getFunctions(true)) {
                if (monitor.isCancelled()) break;
                DecompileResults result = decompiler.decompileFunction(fn, 30, monitor);
                HighFunction high = result.getHighFunction();
                if (high == null) continue;
                boolean matched = false;
                for (java.util.Iterator<PcodeOpAST> it = high.getPcodeOps(); it.hasNext() && !matched;) {
                    PcodeOpAST op = it.next();
                    for (Varnode input : op.getInputs()) {
                        if (input.isConstant()) {
                            long value = input.getOffset();
                            if (value >= TABLE_START && value < TABLE_END) {
                                matched = true;
                                if (hits.add(fn)) {
                                    println("PCODE constant 0x" + Long.toHexString(value) +
                                        " in " + fn.getName() + " @ " + fn.getEntryPoint());
                                }
                                break;
                            }
                        }
                    }
                }
            }
        } finally {
            decompiler.dispose();
        }

        println("TOTAL_CONSUMERS=" + hits.size());
        for (Function fn : hits) {
            println("FUNCTION " + fn.getEntryPoint() + " " + fn.getName());
        }
    }
}
