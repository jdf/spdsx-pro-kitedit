// portlog.js — capture how the SPD-SX PRO App opens & configures its serial
// port. Spawn the app under Frida so we see startup:
//   frida -f "/Applications/SPD-SX PRO App.app/Contents/MacOS/SPD-SX PRO App" -l portlog.js
//
// Logs: which /dev node is opened, the fd, all termios/baud ioctls on it,
// and the first writes/reads so we can see any handshake.

function res(sym){ try { return Module.findGlobalExportByName(sym); } catch(e){ return null; } }
const hex = p => { try { return p.readByteArray(0); } catch(e){ return null; } };
const serialFds = new Set();
const fdName = {};

// --- open: learn which node -> which fd ---
["open","open$NOCANCEL","open_dprotected_np"].forEach(sym=>{
  const p = res(sym); if(!p) return;
  Interceptor.attach(p,{
    onEnter(a){ try { this.path = a[0].readUtf8String(); } catch(e){ this.path=null; } },
    onLeave(r){
      const fd = r.toInt32();
      if(this.path && this.path.indexOf("usbmodem")!==-1 && fd>=0){
        serialFds.add(fd); fdName[fd]=this.path;
        console.log(`\n[open] ${this.path} -> fd ${fd}`);
      }
    }
  });
});

// --- ioctl: baud / line settings often go through here on macOS ---
const ioctl = res("ioctl");
if(ioctl) Interceptor.attach(ioctl,{
  onEnter(a){ this.fd=a[0].toInt32(); this.req=a[1].toUInt32(); },
  onLeave(r){
    if(serialFds.has(this.fd))
      console.log(`[ioctl] fd ${this.fd} req=0x${this.req.toString(16)} ret=${r.toInt32()}`);
  }
});

// --- tcsetattr / cfsetspeed: termios config ---
["tcsetattr","cfsetspeed","cfsetispeed","cfsetospeed"].forEach(sym=>{
  const p=res(sym); if(!p) return;
  Interceptor.attach(p,{
    onEnter(a){
      const fd=a[0].toInt32();
      if(sym==="tcsetattr" && serialFds.has(fd))
        console.log(`[${sym}] fd ${fd}`);
      else if(sym.indexOf("speed")>=0)
        console.log(`[${sym}] speed=${a[1].toInt32?a[1].toInt32():a[1]}`);
    }
  });
});

// --- writes/reads on the serial fd(s) ---
function io(sym, dir){
  const p=res(sym); if(!p) return;
  Interceptor.attach(p,{
    onEnter(a){ this.fd=a[0].toInt32(); this.buf=a[1]; this.n=a[2].toInt32(); },
    onLeave(r){
      if(!serialFds.has(this.fd)) return;
      const n = dir==="write"?this.n:r.toInt32();
      if(n>0 && n<2048){
        console.log(`[${dir} fd ${this.fd} ${fdName[this.fd]||''}] ${n}B:`);
        console.log(hexdump(this.buf,{length:Math.min(n,256),ansi:false}));
      }
    }
  });
}
["write","write$NOCANCEL"].forEach(s=>io(s,"write"));
["read","read$NOCANCEL"].forEach(s=>io(s,"read"));

console.log("portlog ready — the app is starting; watch for [open]/[ioctl]/[write]");
