// bulkread.js — capture the SPD-SX PRO "load current state" bulk-read
// HANDSHAKE. Unlike statelog.js, this KEEPS the 41 6a polls (they are the
// suspected flow-control that pulls each block) and TRUNCATES the ~64KB
// block bodies to their headers, so the log stays small and the
// request -> poll -> block ordering is legible. Every line is timestamped
// (ms since arm) to make the sequence unambiguous.
function res(s){ try { return Module.findGlobalExportByName(s); } catch(e){ return null; } }

const t0 = Date.now();
function ts(){ return ("" + (Date.now() - t0)).padStart(6, ' '); }

// Serial fds: seeded by open(usbmodem) and by any fd we see a write-frame
// on — so attaching AFTER the port was opened still works. Once an fd is
// known, ALL its reads/writes log (block reads mid-stream don't each start
// with the frame magic, so content-matching alone would miss them).
const fds = new Set();
["open","open$NOCANCEL"].forEach(s=>{ const p=res(s); if(!p) return;
  Interceptor.attach(p,{
    onEnter(a){ try{ this.p=a[0].readUtf8String(); }catch(e){ this.p=null; } },
    onLeave(r){ const fd=r.toInt32();
      if(this.p && this.p.indexOf("usbmodem")!==-1 && fd>=0){ fds.add(fd);
        console.log(ts()+" [open] fd="+fd+"  "+this.p); } }
  });
});

function h(b){ return Array.from(b, x=>x.toString(16).padStart(2,'0')).join(' '); }

// Transport frame magic: writes start 0d 60 e0, reads 0d e0 60.
function isWriteFrame(b){
  return b.length>=3 && b[0]==0x0d && b[1]==0x60 && b[2]==0xe0;
}

// Writes (requests/polls) are small and are the point — log them WHOLE.
// Reads (block bodies) are ~64KB; truncate to a head slice.
const READ_HEAD = 48;
function dump(dir, fd, buf, n){
  if(n<=0) return;
  const isRead = dir[0] === '<';
  const take = isRead ? Math.min(n, READ_HEAD) : n;
  const b = new Uint8Array(buf.readByteArray(take));
  const more = n > take ? "  (+"+(n-take)+"B)" : "";
  console.log(ts()+" "+dir+" fd="+fd+" n="+n+more+"\n    "+h(b));
}

["write","write$NOCANCEL"].forEach(s=>{ const p=res(s); if(!p) return;
  Interceptor.attach(p,{
    onEnter(a){ this.fd=a[0].toInt32(); this.buf=a[1]; },
    onLeave(r){ const n=r.toInt32();
      if(n>0 && !fds.has(this.fd)){
        const peek=new Uint8Array(this.buf.readByteArray(Math.min(n,3)));
        if(isWriteFrame(peek)){ fds.add(this.fd);
          console.log(ts()+" [serial fd="+this.fd+" via write-frame]"); }
      }
      if(fds.has(this.fd)) dump(">> WRITE", this.fd, this.buf, n); }
  });
});

["read","read$NOCANCEL"].forEach(s=>{ const p=res(s); if(!p) return;
  Interceptor.attach(p,{
    onEnter(a){ this.fd=a[0].toInt32(); this.buf=a[1]; },
    onLeave(r){ if(fds.has(this.fd)) dump("<< READ ", this.fd, this.buf, r.toInt32()); }
  });
});

console.log(ts()+" [bulkread] armed — load current state in the app.");
