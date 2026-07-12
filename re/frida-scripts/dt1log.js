function res(s){ try { return Module.findGlobalExportByName(s); } catch(e){ return null; } }
const fds = new Set();
["open","open$NOCANCEL"].forEach(s=>{ const p=res(s); if(!p) return;
  Interceptor.attach(p,{ onEnter(a){ try{this.p=a[0].readUtf8String();}catch(e){this.p=null;} },
    onLeave(r){ const fd=r.toInt32(); if(this.p&&this.p.indexOf("usbmodem")!==-1&&fd>=0){ fds.add(fd);} } }); });
function h(a){ return Array.from(a,b=>b.toString(16).padStart(2,'0')).join(' '); }
["write","write$NOCANCEL"].forEach(sym=>{ const p=res(sym); if(!p) return;
  Interceptor.attach(p,{
    onEnter(a){ this.fd=a[0].toInt32(); this.buf=a[1]; this.n=a[2].toInt32(); },
    onLeave(){
      if(!fds.has(this.fd) || this.n<24 || this.n>2048) return;
      const raw=new Uint8Array(this.buf.readByteArray(this.n));
      if(!(raw[20]==0xf0 && raw[21]==0x41 && raw[22]==0x10)) return;
      console.log(`\nDT1 FRAME (${this.n}B):`);
      console.log(`  hdr[0:4]   ${h(raw.slice(0,4))}`);
      console.log(`  hdr[4:8]   ${h(raw.slice(4,8))}`);
      console.log(`  hdr[8:12]  ${h(raw.slice(8,12))}`);
      console.log(`  hdr[12:16] ${h(raw.slice(12,16))}`);
      console.log(`  len[16:20] ${h(raw.slice(16,20))}`);
      console.log(`  payload    ${h(raw.slice(20))}`);
      console.log(`  FULL       ${h(raw)}`);
    }
  });
});
console.log("dt1log ready — change ONE pad-link group; paste the DT1 FRAME block(s).");
