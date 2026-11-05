import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.file.*;
public class ConduitUiProbe extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();
  println("Conduit targeted probe: no whole-program auto-analysis; .pdata-bounded caller selections");
  StringBuilder out=new StringBuilder("IMAGE_BASE "+currentProgram.getImageBase()+"\n");
  java.util.List<Function> functions=new java.util.ArrayList<>();
  for(String line:Files.readAllLines(Paths.get(args[0]))) {
   if(line.isBlank()||line.startsWith("#"))continue;
   String[] parts=line.split("\\s+");
   Address start=toAddr(parts[0]),end=toAddr(parts[1]).subtract(1);
   AddressSet body=new AddressSet(start,end);
   DisassembleCommand dis=new DisassembleCommand(start,body,true);dis.applyTo(currentProgram,monitor);
   Function f=getFunctionAt(start);
   if(f==null){CreateFunctionCmd create=new CreateFunctionCmd(null,start,body,SourceType.ANALYSIS);create.applyTo(currentProgram,monitor);f=getFunctionAt(start);}
   if(f==null){out.append("FAILED "+start+"\n");continue;}
   functions.add(f);
   out.append("FUNCTION "+start+" END_EXCLUSIVE "+toAddr(parts[1])+" BOUND_SOURCE PE_PDATA\n");
   InstructionIterator instructions=currentProgram.getListing().getInstructions(body,true);
   while(instructions.hasNext()){
    Instruction i=instructions.next();
    out.append(i.getAddress()+" "+i+"\n");
    for(Reference r:i.getReferencesFrom())out.append(" REF "+r.getReferenceType()+" "+r.getToAddress()+"\n");
   }
  }
  DecompInterface decompiler=new DecompInterface();
  try{
   decompiler.openProgram(currentProgram);
   for(Function f:functions){
    println("Decompiling "+f.getEntryPoint());
    DecompileResults result=decompiler.decompileFunction(f,60,monitor);
    out.append("PSEUDOCODE "+f.getEntryPoint()+"\n");
    if(result.decompileCompleted())out.append(result.getDecompiledFunction().getC());else out.append("FAILED "+result.getErrorMessage());
    out.append("\n");
   }
  } finally {decompiler.dispose();}
  Files.writeString(Paths.get(args[1]),out.toString());
  println("Wrote targeted output "+args[1]);
 }
}
