// statelog.js — capture BOTH directions of SPD-SX PRO serial traffic while
// the app loads current device state. Hooks read + write on the usbmodem fd,
// suppresses the known heartbeat poll, and annotates transport frames and
// Roland DT1/RQ1 headers.
function res(s){ try { return Module.findGlobalExportByName(s); } catch(e){ return null; } }

const fds = new Set();
["open","open$NOCANCEL"].forEach(s=>{ const p=res(s); if(!p) return;
  Interceptor.attach(p,{
    onEnter(a){ try{ this.p=a[0].readUtf8String(); }catch(e){ this.p=null; } },
    onLeave(r){ const fd=r.toInt32();
      if(this.p && this.p.indexOf("usbmodem")!==-1 && fd>=0){ fds.add(fd);
        console.log("[open] fd="+fd+"  "+this.p); } }
  });
});

function h(b){ return Array.from(b, x=>x.toString(16).padStart(2,'0')).join(' '); }

// Known heartbeat/status poll: f0 41 6a 03 16 00...  — suppress to cut noise.
function isPoll(b){
  if(b.length<6) return false;
  if(!(b[0]==0xf0 && b[1]==0x41 && b[2]==0x6a && b[3]==0x03)) return false;
  for(let i=5;i<b.length-1;i++){ if(b[i]!==0) return false; }
  return true;
}

function annotate(b){
  let note = "";
  if(b.length>=20 && b[0]==0x0d && b[1]==0x60 && b[2]==0xe0){
    const len = b[16] | (b[17]<<8) | (b[18]<<16) | (b[19]*0x1000000);
    note += "  frame ch=0x"+b[3].toString(16)+" len="+len;
  }
  for(let i=0;i+8<b.length;i++){
    if(b[i]==0xf0 && b[i+1]==0x41 && b[i+2]==0x10){
      note += "  roland cmd=0x"+b[i+8].toString(16)+"@"+i; break;
    }
  }
  return note;
}

function dump(dir, fd, buf, n){
  if(n<=0) return;
  const b = new Uint8Array(buf.readByteArray(n));
  if(isPoll(b)) return;
  console.log(dir+" fd="+fd+" n="+n+annotate(b)+"\n    "+h(b));
}

["write","write$NOCANCEL"].forEach(s=>{ const p=res(s); if(!p) return;
  Interceptor.attach(p,{
    onEnter(a){ this.fd=a[0].toInt32(); this.buf=a[1]; },
    onLeave(r){ if(fds.has(this.fd)) dump(">> WRITE", this.fd, this.buf, r.toInt32()); }
  });
});

["read","read$NOCANCEL"].forEach(s=>{ const p=res(s); if(!p) return;
  Interceptor.attach(p,{
    onEnter(a){ this.fd=a[0].toInt32(); this.buf=a[1]; },
    onLeave(r){ if(fds.has(this.fd)) dump("<< READ ", this.fd, this.buf, r.toInt32()); }
  });
});

console.log("[statelog] armed — hit the 'load current state' button in the app.");
