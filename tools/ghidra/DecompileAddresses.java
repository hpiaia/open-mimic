// Decompile one or more hexadecimal addresses supplied as script arguments.
// @category OpenMimic

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DecompileAddresses extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        for (String argument : getScriptArgs()) {
            Address address = currentProgram.getAddressFactory().getDefaultAddressSpace()
                .getAddress(Long.parseUnsignedLong(argument.replaceFirst("^0x", ""), 16));
            Function function = getFunctionContaining(address);
            if (function == null) {
                println("No function at " + address);
                continue;
            }
            println("\n/* " + function.getName(true) + " @ " + function.getEntryPoint() + " */");
            DecompileResults result = decompiler.decompileFunction(function, 120, monitor);
            if (!result.decompileCompleted()) {
                println("Decompile failed: " + result.getErrorMessage());
            } else {
                println(result.getDecompiledFunction().getC());
            }
        }
        decompiler.dispose();
    }
}
