function res(s){ try { return Module.findGlobalExportByName(s); } catch(e){ return null; } }
const fds = new Set();
["open","open$NOCANCEL"].forEach(s=>{ const p=res(s); if(!p) return;
  Interceptor.attach(p,{ onEnter(a){ try{this.p=a[0].readUtf8String();}catch(e){this.p=null;} },
    onLeave(r){ const fd=r.toInt32(); if(this.p&&this.p.indexOf("usbmodem")!==-1&&fd>=0){ fds.add(fd);} } }); });

function h(a){ return Array.from(a,b=>b.toString(16).padStart(2,'0')).join(' '); }

function isNoisyPoll(pay){
  if(pay.length<6) return false;
  if(!(pay[0]==0xf0 && pay[1]==0x41 && pay[2]==0x6a && pay[3]==0x03)) return false;
  for(let i=5;i<pay.length-1;i++){ if(pay[i]!==0) return false; }
  return true;
}

["write","write$NOCANCEL"].forEach(sym=>{ const p=res(sym); if(!p) return;
  Interceptor.attach(p,{
    onEnter(a){ this.fd=a[0].toInt32(); this.buf=a[1]; this.n=a[2].toInt32(); },
    onLeave(){
      if(!fds.has(this.fd) || this.n<20 || this.n>2048) return;
      const raw=new Uint8Array(this.buf.readByteArray(this.n));
      const ln = raw[16]|(raw[17]<<8)|(raw[18]<<16)|(raw[19]<<24);
      const pay = raw.slice(20,20+ln);
      if(isNoisyPoll(pay)) return;
      const isDT1 = pay.length>=9 && pay[1]==0x41 && pay[2]==0x10 && pay[8]==0x12;
      console.log(`\n${isDT1?'*** DT1 ***':'WRITE'} ${this.n}B payload(${ln}): ${h(pay)}`);
    }
  });
});
console.log("editlog v2 ready — polls suppressed. Change ONE pad-link group now.");
