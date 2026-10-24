// Import-time private captured-code overlay and bounded selected-function audit.
// @category FNV.Analysis
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;

public class AuditCapturedFunctions extends GhidraScript {
    private static String hex(byte[] bytes) {
        StringBuilder out=new StringBuilder();
        for(byte b:bytes)out.append(String.format("%02x",b & 255));
        return out.toString();
    }
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();
        if(args.length!=5)throw new IllegalArgumentException("capture expectedSHA256 textVA outputFile commaSeparatedFunctionVAs");
        byte[] bytes=Files.readAllBytes(Path.of(args[0]));
        String digest=hex(MessageDigest.getInstance("SHA-256").digest(bytes));
        if(!digest.equalsIgnoreCase(args[1]))throw new IllegalArgumentException("Capture hash mismatch");
        Address text=toAddr(Long.parseUnsignedLong(args[2].replaceFirst("^0[xX]",""),16));
        MemoryBlock block=currentProgram.getMemory().getBlock(text);
        if(block==null || !block.isInitialized() || !block.getStart().equals(text) || bytes.length>block.getSize())
            throw new IllegalArgumentException("Capture does not fit initialized code block start");
        currentProgram.getMemory().setBytes(text,bytes);
        DecompInterface decompiler=new DecompInterface();
        if(!decompiler.openProgram(currentProgram))throw new IllegalStateException(decompiler.getLastMessage());
        try(PrintWriter out=new PrintWriter(Files.newBufferedWriter(Path.of(args[3]),StandardCharsets.UTF_8))) {
            out.println("Private selected-function analysis; not generated port code or a correctness oracle.");
            out.println("capture_sha256="+digest+" image_base="+currentProgram.getImageBase());
            for(String name:args[4].split(",")) {
                monitor.checkCancelled();
                Address at=toAddr(Long.parseUnsignedLong(name.replaceFirst("^0[xX]",""),16));
                if(!block.contains(at))throw new IllegalArgumentException("Function outside captured block: "+name);
                Function f=getFunctionAt(at);
                if(f==null){disassemble(at);f=createFunction(at,null);}
                out.println("\n===== "+at+" =====");
                if(f==null){out.println("NO_FUNCTION: inspect boundaries manually");continue;}
                out.println("body="+f.getBody());
                DecompileResults r=decompiler.decompileFunction(f,20,monitor);
                if(r.decompileCompleted() && r.getDecompiledFunction()!=null)out.println(r.getDecompiledFunction().getC());
                else out.println("DECOMPILATION_FAILED: "+r.getErrorMessage());
                out.flush();
            }
        } finally {decompiler.dispose();}
        println("PRIVATE_SELECTED_FUNCTION_AUDIT_COMPLETE");
    }
}
