// paramlog.js — capture DT1 parameter WRITES as the app edits a pad.
// Content-triggered on the frame magic (robust to attaching after the port
// is open), suppresses the 6a heartbeat, decodes each DT1 into its address
// and data bytes, timestamped so writes line up with the edits you make.
//
// DT1 payload: f0 41 10 00 00 00 00 16 12 <addr...> <data...> <cksum> f7
// (addr starts at payload[9]; the object-focus write 28 00 00 00 <sel>
// precedes a per-pad param write, so watch for a focus then the param.)
function res(s){ try { return Module.findGlobalExportByName(s); } catch(e){ return null; } }

const t0 = Date.now();
function ts(){ return ("" + (Date.now() - t0)).padStart(6, ' '); }
function h(a){ return Array.from(a, b=>b.toString(16).padStart(2,'0')).join(' '); }

const fds = new Set();
["open","open$NOCANCEL"].forEach(s=>{ const p=res(s); if(!p) return;
  Interceptor.attach(p,{ onEnter(a){ try{this.p=a[0].readUtf8String();}catch(e){this.p=null;} },
    onLeave(r){ const fd=r.toInt32(); if(this.p&&this.p.indexOf("usbmodem")!==-1&&fd>=0){ fds.add(fd);} } }); });

function isWriteFrame(b){ return b.length>=3 && b[0]==0x0d && b[1]==0x60 && b[2]==0xe0; }

["write","write$NOCANCEL"].forEach(sym=>{ const p=res(sym); if(!p) return;
  Interceptor.attach(p,{
    onEnter(a){ this.fd=a[0].toInt32(); this.buf=a[1]; this.n=a[2].toInt32(); },
    onLeave(){
      if(this.n<20 || this.n>4096) return;
      const raw=new Uint8Array(this.buf.readByteArray(this.n));
      if(!fds.has(this.fd)){ if(!isWriteFrame(raw)) return; fds.add(this.fd); }
      const ln = raw[16]|(raw[17]<<8)|(raw[18]<<16)|(raw[19]<<24);
      const pay = raw.slice(20, 20+ln);
      // Only DT1 param writes: f0 41 10 ... 12 ...
      if(!(pay.length>=10 && pay[1]==0x41 && pay[2]==0x10 && pay[8]==0x12)) return;
      // addr = pay[9 .. ] up to the last two bytes (cksum, f7). We don't know
      // the addr/data split a priori, so show the whole body and a guessed
      // 4-byte addr (the common case).
      const body = pay.slice(9, pay.length-2);   // addr + data (drop cksum, f7)
      const addr4 = body.slice(0, 4);
      const data  = body.slice(4);
      console.log(`${ts()} DT1 addr=[${h(addr4)}] data=[${h(data)}]   full=[${h(pay)}]`);
    }
  });
});
console.log(`${ts()} [paramlog] armed — change one pad param; each DT1 write prints.`);
