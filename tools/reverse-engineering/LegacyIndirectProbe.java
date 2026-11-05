import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import java.nio.file.*;
public class LegacyIndirectProbe extends GhidraScript {
 public void run() throws Exception {
  StringBuilder out=new StringBuilder("Read-only saved-project query; no analysis or decompilation\n");
  long[] sites={0x146583184L,0x1465831a3L,0x1465831a7L,0x1465831aaL,0x1465831b1L,0x140f20479L,0x140f20486L,0x140f2048aL};
  for(long n:sites){Address a=toAddr(n);out.append(a+" "+getInstructionAt(a)+"\n");}
  for(long n:new long[]{0x14f99ef70L,0x14f091bd0L,0x14f091de0L}){
   Address a=toAddr(n);out.append("QWORD "+a+" = "+Long.toHexString(getLong(a))+"\n");
  }
  Files.writeString(Paths.get(getScriptArgs()[0]),out.toString());
 }
}
