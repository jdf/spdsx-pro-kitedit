// writecmd.js — characterize the WRITE-button (flash-commit) handshake.
// Hooks reads AND writes on the usbmodem fd, logs BOTH directions in full,
// and suppresses ONLY the constant 15/16 heartbeat (earlier this also ate
// the 6a-03 cat 21/22 commit commands — do not do that). Timestamped.
// Spawn with -f so open() is seen.
function res(s){ try { return Module.findGlobalExportByName(s); } catch(e){ return null; } }
const t0=Date.now();
function ts(){ return (""+(Date.now()-t0)).padStart(6,' '); }
function h(a){ return Array.from(a,b=>b.toString(16).padStart(2,'0')).join(' '); }

const fds=new Set();
["open","open$NOCANCEL"].forEach(s=>{ const p=res(s); if(!p) return;
  Interceptor.attach(p,{ onEnter(a){ try{this.p=a[0].readUtf8String();}catch(e){this.p=null;} },
    onLeave(r){ const fd=r.toInt32(); if(this.p&&this.p.indexOf("usbmodem")!==-1&&fd>=0){ fds.add(fd);
      console.log(ts()+" [open usbmodem fd="+fd+"]"); } } }); });

// The payload sits after the 20-byte transport frame header (both dirs).
// Heartbeat = 6a 03 with category 15 or 16 and an all-zero body — the only
// thing we hide.
function payloadOf(b){ return b.length>20 ? b.slice(20) : b; }
function isHeartbeat(p){
  if(p.length<6 || p[0]!=0xf0 || p[1]!=0x41 || p[2]!=0x6a || p[3]!=0x03) return false;
  if(p[4]!=0x15 && p[4]!=0x16) return false;
  for(let i=5;i<p.length-1;i++){ if(p[i]!==0) return false; }
  return true;
}
let hb=0;
function log(dir, fd, buf, n){
  if(n<=0) return;
  const b=new Uint8Array(buf.readByteArray(Math.min(n,64)));
  const p=payloadOf(b);
  if(isHeartbeat(p)){ if(++hb % 200 === 0) console.log(ts()+" ("+hb+" heartbeats)"); return; }
  const more=n>b.length ? "  (+"+(n-b.length)+"B)" : "";
  console.log(ts()+" "+dir+" fd="+fd+" n="+n+more+"\n    "+h(b));
}

["write","write$NOCANCEL"].forEach(s=>{ const p=res(s); if(!p) return;
  Interceptor.attach(p,{ onEnter(a){ this.fd=a[0].toInt32(); this.buf=a[1]; },
    onLeave(r){ if(fds.has(this.fd)) log(">> WRITE", this.fd, this.buf, r.toInt32()); } }); });
["read","read$NOCANCEL"].forEach(s=>{ const p=res(s); if(!p) return;
  Interceptor.attach(p,{ onEnter(a){ this.fd=a[0].toInt32(); this.buf=a[1]; },
    onLeave(r){ if(fds.has(this.fd)) log("<< READ ", this.fd, this.buf, r.toInt32()); } }); });

console.log(ts()+" [writecmd] armed — edit, PAUSE, then press WRITE.");
