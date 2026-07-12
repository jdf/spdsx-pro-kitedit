// writecmd.js — capture EVERY outgoing frame family (not just DT1), to find
// the command the app's "WRITE" button sends to persist a kit to storage.
// Content-triggered fd detection (robust to attaching after the port opened),
// suppresses only the 6a heartbeat poll, truncates large payloads,
// timestamped. Make a trivial edit, then press WRITE and watch for a
// non-DT1 command.
function res(s){ try { return Module.findGlobalExportByName(s); } catch(e){ return null; } }
const t0=Date.now();
function ts(){ return (""+(Date.now()-t0)).padStart(6,' '); }
function h(a){ return Array.from(a,b=>b.toString(16).padStart(2,'0')).join(' '); }

const fds=new Set();
["open","open$NOCANCEL"].forEach(s=>{ const p=res(s); if(!p) return;
  Interceptor.attach(p,{ onEnter(a){ try{this.p=a[0].readUtf8String();}catch(e){this.p=null;} },
    onLeave(r){ const fd=r.toInt32(); if(this.p&&this.p.indexOf("usbmodem")!==-1&&fd>=0) fds.add(fd); } }); });

function isWriteFrame(b){ return b.length>=3 && b[0]==0x0d && b[1]==0x60 && b[2]==0xe0; }
// Suppress the idle status poll: f0 41 6a 03 16 00.. (all zero body).
function isPoll(pay){
  if(pay.length<6 || !(pay[0]==0xf0&&pay[1]==0x41&&pay[2]==0x6a&&pay[3]==0x03)) return false;
  for(let i=5;i<pay.length-1;i++){ if(pay[i]!==0) return false; }
  return true;
}
const HEAD=48;
["write","write$NOCANCEL"].forEach(sym=>{ const p=res(sym); if(!p) return;
  Interceptor.attach(p,{
    onEnter(a){ this.fd=a[0].toInt32(); this.buf=a[1]; this.n=a[2].toInt32(); },
    onLeave(){
      if(this.n<20 || this.n>1<<20) return;
      const peek=new Uint8Array(this.buf.readByteArray(Math.min(this.n,3)));
      if(!fds.has(this.fd)){ if(!isWriteFrame(peek)) return; fds.add(this.fd); }
      const raw=new Uint8Array(this.buf.readByteArray(Math.min(this.n,HEAD+20)));
      const ch=raw[3];
      const lenLE=raw[16]|(raw[17]<<8)|(raw[18]<<16)|(raw[19]<<24);
      const pay=raw.slice(20);
      if(isPoll(pay)) return;
      const fam = pay.length>=3 ? `${pay[1].toString(16)} ${pay[2].toString(16)}` : '?';
      const more = this.n>HEAD+20 ? `  (+${this.n-(HEAD+20)}B)` : '';
      console.log(`${ts()} WRITE ch=0x${ch.toString(16)} fam=[41 ${fam}] paylen=${lenLE} n=${this.n}${more}\n    ${h(pay)}`);
    }
  });
});
console.log(`${ts()} [writecmd] armed — make a trivial edit, then press WRITE.`);
