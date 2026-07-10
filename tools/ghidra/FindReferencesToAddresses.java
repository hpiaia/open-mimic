// Print references to one or more hexadecimal addresses.
// @category OpenMimic

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class FindReferencesToAddresses extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String argument : getScriptArgs()) {
            Address target = currentProgram.getAddressFactory().getDefaultAddressSpace()
                .getAddress(Long.parseUnsignedLong(argument.replaceFirst("^0x", ""), 16));
            println("References to " + target + ":");
            ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(target);
            while (references.hasNext()) {
                Reference reference = references.next();
                Function function = getFunctionContaining(reference.getFromAddress());
                println("  " + reference.getFromAddress() + "  " +
                    (function == null ? "<no function>" : function.getName(true)));
            }
        }
    }
}
