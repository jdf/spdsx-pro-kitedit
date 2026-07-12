function resolve(sym){ try { return Module.findGlobalExportByName(sym); } catch(e){ return null; } }
const hex = a => Array.from(a, b => b.toString(16).padStart(2,"0")).join(" ");

function findRoland(buf,len){
  const b = new Uint8Array(buf.readByteArray(len));
  // locate the Roland DT1 header f0 41 10 ... anywhere in the buffer
  for(let i=0;i+2<b.length;i++){
    if(b[i]===0xF0 && b[i+1]===0x41 && b[i+2]===0x10){
      const e = b.indexOf(0xF7, i); if(e<0) return null;
      return b.slice(i, e+1);
    }
  }
  return null;
}

Interceptor.attach(resolve("write"),{
  onEnter(a){ this.buf=a[1]; this.len=a[2].toInt32(); },
  onLeave(){
    if(this.len<=0||this.len>=8192) return;
    const sx=findRoland(this.buf,this.len); if(!sx) return;
    if(sx[8]!==0x12) return;                 // DT1 writes only
    const addr = Array.from(sx.slice(9,15));
    const data = Array.from(sx.slice(15, sx.length-2)); // between addr and checksum
    console.log(`DT1 addr=${hex(addr)}  data=${hex(data)}  | ${hex(sx)}`);
  }
});
console.log("roland DT1 logger ready (junk-prefix aware)");
