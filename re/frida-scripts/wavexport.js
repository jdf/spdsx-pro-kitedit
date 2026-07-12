// wavexport.js — capture the user-wave EXPORT (remote file read) protocol.
// Like bulkread.js but tuned to decode the f0 41 7a / channel-0x06
// file-transfer family: it does NOT truncate read bodies (READ_HEAD is
// large), so the multi-block read loop and the .SMP/RFWV payload of a
// LARGER user sample are fully reconstructable. Export one big user wave
// after arming. See re-cache/captures/WAVE-EXPORT-PROTOCOL.md.
function res(s){ try { return Module.findGlobalExportByName(s); } catch(e){ return null; } }

const t0 = Date.now();
function ts(){ return ("" + (Date.now() - t0)).padStart(6, ' '); }

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
function isWriteFrame(b){ return b.length>=3 && b[0]==0x0d && b[1]==0x60 && b[2]==0xe0; }

// Full reads: the whole point here is the data payload. 64KB ceiling
// keeps a runaway state-dump from flooding, but a wave read fits.
const READ_HEAD = 1 << 16;
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

console.log(ts()+" [wavexport] armed — load state, then EXPORT one large user wave.");
