// framelog.js — focus on the 16-byte frame header across many writes, to
// separate constant fields from counters/handles.
//   frida -f "/Applications/SPD-SX PRO App.app/Contents/MacOS/SPD-SX PRO App" -l framelog.js
// Then: idle a moment, then change a few pad-link settings.

function res(sym){ try { return Module.findGlobalExportByName(sym); } catch(e){ return null; } }
const serialFds = new Set();

["open","open$NOCANCEL"].forEach(sym=>{
  const p=res(sym); if(!p) return;
  Interceptor.attach(p,{
    onEnter(a){ try{ this.path=a[0].readUtf8String(); }catch(e){ this.path=null; } },
    onLeave(r){ const fd=r.toInt32();
      if(this.path && this.path.indexOf("usbmodem")!==-1 && fd>=0){ serialFds.add(fd);
        console.log(`[open] ${this.path} fd ${fd}`); } }
  });
});

function b2h(arr){ return Array.from(arr, b=>b.toString(16).padStart(2,'0')).join(' '); }

let n=0;
function hookW(sym){
  const p=res(sym); if(!p) return;
  Interceptor.attach(p,{
    onEnter(a){ this.fd=a[0].toInt32(); this.buf=a[1]; this.len=a[2].toInt32(); },
    onLeave(){
      if(!serialFds.has(this.fd) || this.len<20 || this.len>2048) return;
      const raw = new Uint8Array(this.buf.readByteArray(this.len));
      const hdr = raw.slice(0,16);
      const lenLE = raw[16]|(raw[17]<<8)|(raw[18]<<16)|(raw[19]<<24);
      const payload = raw.slice(20, 20+lenLE);
      console.log(`\n#${n++} WRITE len=${this.len}`);
      console.log(`  hdr[0:4]  ${b2h(hdr.slice(0,4))}`);
      console.log(`  hdr[4:8]  ${b2h(hdr.slice(4,8))}  (${String.fromCharCode(...hdr.slice(4,8)).replace(/[^ -~]/g,'.')})`);
      console.log(`  hdr[8:12] ${b2h(hdr.slice(8,12))}`);
      console.log(`  hdr[12:16]${b2h(hdr.slice(12,16))}`);
      console.log(`  paylen=${lenLE}  payload ${b2h(payload)}`);
    }
  });
}
["write","write$NOCANCEL"].forEach(hookW);
console.log("framelog ready — idle, then change several pad-link groups");
