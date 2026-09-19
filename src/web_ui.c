// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/web_ui.h"

#include <string.h>

static const char page_part1a[] =
"<!doctype html>\n"
"<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
"<title>flex1500d operator test</title><style>\n"
":root{color-scheme:dark;background:#10151b;color:#e7edf4;font:16px system-ui,sans-serif}\n"
"body{max-width:760px;margin:3rem auto;padding:0 1rem}fieldset{border:1px solid #405064;border-radius:.6rem;padding:1rem}\n"
"label{display:block;margin:.7rem 0}.row{display:flex;gap:.6rem;flex-wrap:wrap;align-items:end}\n"
"input,select,button{font:inherit;padding:.55rem .7rem;border-radius:.35rem;border:1px solid #60758c}\n"
"input,select{background:#17212b;color:inherit}input{width:12rem}button{background:#254b70;color:white;cursor:pointer}\n"
"button.stop{background:#713b3b}pre{white-space:pre-wrap;background:#17212b;padding:1rem;border-radius:.5rem;min-height:5rem}\n"
".safe{color:#8fd19e}.warn{color:#ffd479}</style></head><body>\n"
"<h1>FLEX-1500 operator test</h1>\n"
"<p class=\"warn\">Experimental software. Transmit controls operate only when the daemon was explicitly started in TX mode.</p>\n"
"<fieldset><legend>Radio</legend><div class=\"row\">\n"
"<label>Frequency (Hz)<br><input id=\"frequency\" type=\"number\" min=\"100000\" max=\"54000000\" step=\"1\" value=\"10000000\"></label>\n"
"<label>Mode<br><select id=\"mode\"><option>AM</option><option>USB</option><option>LSB</option><option>FM</option><option>CW</option></select></label>\n"
"<label>DSP bandwidth (Hz)<br><input id=\"bandwidth\" type=\"number\" min=\"100\" max=\"20000\" step=\"100\" value=\"6000\"></label>\n"
"<label>Squelch (dBFS)<br><input id=\"squelch\" type=\"number\" min=\"-120\" max=\"0\" step=\"1\" value=\"-120\"></label>\n"
"<label>RX gain<br><select id=\"gain\"><option value=\"-10\">-10 dB</option><option value=\"0\">0 dB</option><option value=\"10\">+10 dB</option><option value=\"20\" selected>+20 dB</option><option value=\"30\">+30 dB</option></select></label>\n"
"<button id=\"tune\">Tune RX</button><button id=\"refresh\">Refresh status</button></div>\n"
"<div class=\"row\"><button id=\"start\">Start audio</button><button class=\"stop\" id=\"stop\">Stop audio</button></div>\n"
"<p class=\"warn\">Changing mode or DSP bandwidth stops audio. Tuning remains continuous. CW uses a 700 Hz receive tone.</p></fieldset>\n";

static const char page_part1b[] =
"<fieldset><legend>Experimental transmit</legend><p class=\"warn\">Use only with a suitable load and legal frequency. TX leases are not authentication.</p><div class=\"row\">\n"
"<label>Drive (%)<br><input id=\"txdrive\" type=\"number\" min=\"1\" max=\"100\" value=\"100\"></label>\n"
"<label>Mic gain (dB)<br><input id=\"txmicgain\" type=\"number\" min=\"0\" max=\"70\" value=\"10\"></label>\n"
"<label>TX timeout (s)<br><input id=\"txtimeout\" type=\"number\" min=\"30\" max=\"1800\" value=\"180\"></label>\n"
"<label><input id=\"txcompressor\" type=\"checkbox\" style=\"width:auto\"> Speech compressor</label></div>\n"
"<div class=\"row\"><button id=\"tuneStart\">Start 5 W Tune</button><button class=\"stop\" id=\"tuneStop\">Stop Tune</button><button id=\"micStart\">Start computer mic TX</button><button class=\"stop\" id=\"micStop\">Stop mic TX</button></div>\n"
"<p id=\"micSecurity\" class=\"warn\"></p></fieldset>\n"
"<h2>Status</h2><pre id=\"status\">Loading…</pre>\n"
"<script>\n"
"const statusBox=document.querySelector('#status');let aborter=null,context=null,rxOutput=null,underruns=0,tuneLease=null,tuneTimer=null,txLease=null,txTimer=null,txContext=null,txStream=null,txNode=null,txUpload=Promise.resolve(),txKeyed=false,tuneAudioMuted=false,controlLease=null,controlOwner=false,controlTimer=null,hardwareHz=0,virtualOffsetHz=0;\n"
"function show(v){statusBox.textContent=typeof v==='string'?v:JSON.stringify(v,null,2)}\n"
"function muteRxForTune(muted){tuneAudioMuted=muted;if(rxOutput)rxOutput.gain.setValueAtTime(muted?0:1,context.currentTime)}\n"
"async function refresh(){try{const r=await fetch('/v1/radio'),radio=await r.json();const first=hardwareHz===0;hardwareHz=radio.frequency_hz||hardwareHz;if((controlOwner||first)&&radio.frequency_hz)document.querySelector('#frequency').value=radio.frequency_hz;if((controlOwner||first)&&radio.rx_mode)document.querySelector('#mode').value=radio.rx_mode.toUpperCase();if(radio.rx_gain_db!==null)document.querySelector('#gain').value=radio.rx_gain_db;if((controlOwner||first)&&radio.rx_bandwidth_hz)document.querySelector('#bandwidth').value=radio.rx_bandwidth_hz;if(radio.rx_squelch_db!==undefined)document.querySelector('#squelch').value=radio.rx_squelch_db;if(radio.tx_drive_percent)document.querySelector('#txdrive').value=radio.tx_drive_percent;if(radio.tx_microphone_gain_db!==undefined)document.querySelector('#txmicgain').value=radio.tx_microphone_gain_db;if(radio.tx_timeout_seconds)document.querySelector('#txtimeout').value=radio.tx_timeout_seconds;document.querySelector('#txcompressor').checked=!!radio.tx_compressor_enabled;const s=await fetch('/v1/status'),service=await s.json();show({radio,service})}catch(e){show('Status error: '+e)}}\n"
"async function stopAudio(){if(aborter)aborter.abort();aborter=null;if(context)await context.close();context=null;rxOutput=null}\n"
"document.querySelector('#refresh').onclick=refresh;document.querySelector('#stop').onclick=stopAudio;\n"
"document.querySelector('#tune').onclick=async()=>{const hz=document.querySelector('#frequency').value;try{const r=await fetch('/v1/radio/frequency/'+hz,{method:'PUT'});const body=await r.json();if(!r.ok)throw new Error(JSON.stringify(body));show(body);await refresh()}catch(e){show('Tune error: '+e)}};\n";

static const char page_part2[] =
"document.querySelector('#mode').onchange=async()=>{await stopAudio();const mode=document.querySelector('#mode').value.toLowerCase();try{if(!controlOwner){show('Receive-only local mode: '+mode.toUpperCase());return}const body=await api('/v1/radio/mode/'+mode);show(body);await refresh()}catch(e){show('Mode error: '+e)}};\n"
"document.querySelector('#gain').onchange=async()=>{const gain=document.querySelector('#gain').value;try{const r=await fetch('/v1/radio/gain/'+gain,{method:'PUT'});const body=await r.json();if(!r.ok)throw new Error(JSON.stringify(body));show(body);await refresh()}catch(e){show('Gain error: '+e)}};\n"
"for(const id of ['bandwidth','squelch'])document.querySelector('#'+id).onchange=async()=>{await stopAudio();const value=document.querySelector('#'+id).value;try{if(!controlOwner){show('Receive-only local '+id+': '+value);return}const body=await api('/v1/radio/'+id+'/'+value);show(body);await refresh()}catch(e){show(id+' error: '+e)}};\n"
"document.querySelector('#start').onclick=async()=>{try{await stopAudio();\n"
"const mode=document.querySelector('#mode').value.toLowerCase();\n"
"const bandwidth=Number(document.querySelector('#bandwidth').value),squelch=Number(document.querySelector('#squelch').value);\n"
"context=new AudioContext({sampleRate:48000});if(context.sampleRate!==48000)throw new Error('Browser audio rate is '+context.sampleRate+', expected 48000');\n";

static const char page_part2b[] =
"let node;underruns=0;if(context.audioWorklet&&typeof AudioWorkletNode!=='undefined'){const worklet=`class Q extends AudioWorkletProcessor{constructor(){super();this.q=[];this.o=0;this.buffered=0;this.playing=false;this.underruns=0;this.port.onmessage=e=>{const b=new Float32Array(e.data);this.q.push(b);this.buffered+=b.length;if(this.buffered>=4096)this.playing=true}}process(i,o){const a=o[0][0];a.fill(0);if(!this.playing)return true;let p=0;while(p<a.length&&this.q.length){const b=this.q[0],n=Math.min(a.length-p,b.length-this.o);a.set(b.subarray(this.o,this.o+n),p);p+=n;this.o+=n;this.buffered-=n;if(this.o===b.length){this.q.shift();this.o=0}}if(p<a.length){this.playing=false;this.underruns++;this.port.postMessage({underruns:this.underruns})}return true}}registerProcessor('f15-audio',Q)`;const url=URL.createObjectURL(new Blob([worklet],{type:'application/javascript'}));await context.audioWorklet.addModule(url);URL.revokeObjectURL(url);node=new AudioWorkletNode(context,'f15-audio',{outputChannelCount:[1]});node.port.onmessage=e=>{if(e.data&&e.data.underruns!==undefined)underruns=e.data.underruns}}else{const q=[],state={offset:0,buffered:0,playing:false};node=context.createScriptProcessor(1024,0,1);node.port={postMessage:data=>{const b=new Float32Array(data);q.push(b);state.buffered+=b.length;if(state.buffered>=4096)state.playing=true}};node.onaudioprocess=e=>{const a=e.outputBuffer.getChannelData(0);a.fill(0);if(!state.playing)return;let p=0;while(p<a.length&&q.length){const b=q[0],n=Math.min(a.length-p,b.length-state.offset);a.set(b.subarray(state.offset,state.offset+n),p);p+=n;state.offset+=n;state.buffered-=n;if(state.offset===b.length){q.shift();state.offset=0}}if(p<a.length){state.playing=false;underruns++}}}\n"
"rxOutput=context.createGain();rxOutput.gain.value=tuneAudioMuted?0:1;node.connect(rxOutput);rxOutput.connect(context.destination);await context.resume();\n"
"aborter=new AbortController();const response=await fetch('/v1/stream/iq',{signal:aborter.signal});if(!response.ok)throw new Error('IQ stream HTTP '+response.status);\n"
"const reader=response.body.getReader();let buffered=new Uint8Array(0),dc=0,lp=0,env=1,rfenv=1,phase=0,vphase=0,pi=0,pq=0,havePrev=false,settle=2400,frames=0;\n"
"const nt=129,hi=new Float32Array(nt),hq=new Float32Array(nt),ci=new Float32Array(nt),cq=new Float32Array(nt);let hp=0;if(mode==='usb'||mode==='lsb'||mode==='cw'){const low=mode==='cw'?Math.max(0,700-bandwidth/2):100,high=mode==='cw'?Math.min(12000,700+bandwidth/2):Math.min(12000,low+bandwidth),cutoff=(high-low)/2,center=(low+high)/2*(mode==='lsb'?-1:1),mid=(nt-1)/2;let sum=0;for(let t=0;t<nt;t++){const k=t-mid,w=.54-.46*Math.cos(2*Math.PI*t/(nt-1)),b=(k===0?2*cutoff/48000:Math.sin(2*Math.PI*cutoff*k/48000)/(Math.PI*k))*w;ci[t]=b*Math.cos(2*Math.PI*center*k/48000);cq[t]=b*Math.sin(2*Math.PI*center*k/48000);sum+=b}for(let t=0;t<nt;t++){ci[t]/=sum;cq[t]/=sum}}\n";

static const char page_part3[] =
"while(true){const {value,done}=await reader.read();if(done)break;const joined=new Uint8Array(buffered.length+value.length);joined.set(buffered);joined.set(value,buffered.length);buffered=joined;\n"
"while(buffered.length>=20){const v=new DataView(buffered.buffer,buffered.byteOffset);if(v.getUint32(0,false)!==0x46313549||v.getUint8(4)!==1||v.getUint8(5)!==1||v.getUint16(6,false)!==20||v.getUint32(12,false)!==48000)throw new Error('Invalid F15I frame');\n"
"const count=v.getUint32(16,false),length=20+count*8;if(buffered.length<length)break;const audio=new Float32Array(count);\n"
"for(let n=0;n<count;n++){let i=v.getFloat32(20+n*8,true),q=v.getFloat32(24+n*8,true);if(virtualOffsetHz!==0){const c=Math.cos(vphase),s=Math.sin(vphase),ri=i*c-q*s,rq=i*s+q*c;i=ri;q=rq;vphase+=2*Math.PI*virtualOffsetHz/48000;if(vphase>Math.PI)vphase-=2*Math.PI;if(vphase< -Math.PI)vphase+=2*Math.PI}rfenv+=.002*(Math.hypot(i,q)-rfenv);let x=0;if(mode==='am'){x=Math.hypot(i,q)}else if(mode==='fm'){if(havePrev)x=Math.atan2(pi*q-pq*i,pi*i+pq*q);pi=i;pq=q;havePrev=true}else{hi[hp]=i;hq[hp]=q;let fi=0,fq=0,p=hp;for(let t=0;t<nt;t++){fi+=hi[p]*ci[t]-hq[p]*cq[t];fq+=hi[p]*cq[t]+hq[p]*ci[t];p=p===0?nt-1:p-1}hp=(hp+1)%nt;if(mode==='cw'){phase+=2*Math.PI*700/48000;x=fi*Math.cos(phase)-fq*Math.sin(phase)}else{x=fi}}if(phase>Math.PI)phase-=2*Math.PI;dc+=.0005*(x-dc);x-=dc;lp+=.25*(x-lp);env=Math.max(Math.abs(lp),env*.9995);const level=20*Math.log10((rfenv+1e-9)/32768);audio[n]=settle-->0||level<squelch?0:Math.max(-.8,Math.min(.8,.24*lp/(env+1e-6)))}\n"
"node.port.postMessage(audio.buffer,[audio.buffer]);frames++;buffered=buffered.slice(length);if(frames%100===0)show(mode.toUpperCase()+' audio running\\nF15I frames: '+frames+'\\nAudio rate: '+context.sampleRate+' Hz\\nAudio underruns: '+underruns)} }\n"
"}catch(e){if(e.name!=='AbortError')show('Audio error: '+e);await stopAudio()}};\n";

static const char page_part4a[] =
"async function api(path,method='PUT',lease=null,body=null){const h={};if(controlLease!==null)h['X-Flex1500-Control-Lease']=String(controlLease);if(lease!==null)h['X-Flex1500-TX-Lease']=String(lease);if(body!==null)h['Content-Type']=body instanceof ArrayBuffer?'application/octet-stream':'application/json';const r=await fetch(path,{method,headers:h,body:body instanceof ArrayBuffer?body:(body===null?undefined:JSON.stringify(body))});const j=await r.json();if(!r.ok)throw new Error(JSON.stringify(j));return j}\n"
"async function applyTxSettings(){await api('/v1/radio/tx-drive/'+document.querySelector('#txdrive').value);await api('/v1/radio/mic-gain/'+document.querySelector('#txmicgain').value);await api('/v1/radio/tx-timeout/'+document.querySelector('#txtimeout').value);await api('/v1/radio/tx-compressor/'+(document.querySelector('#txcompressor').checked?'on':'off'))}\n"
"document.querySelector('#tuneStart').onclick=async()=>{try{await applyTxSettings();muteRxForTune(true);const j=await api('/v1/radio/tune/start');tuneLease=j.lease;tuneTimer=setInterval(()=>api('/v1/radio/tune/keepalive/'+tuneLease).catch(e=>show('Tune keepalive error: '+e)),5000);show('Tune transmitting; browser RX audio muted')}catch(e){if(tuneLease===null)muteRxForTune(false);show('Tune start error: '+e)}};\n"
"document.querySelector('#tuneStop').onclick=async()=>{try{if(tuneTimer)clearInterval(tuneTimer);tuneTimer=null;if(tuneLease!==null)await api('/v1/radio/tune/stop/'+tuneLease);tuneLease=null;muteRxForTune(false);show('Tune stopped; browser RX audio restored');await refresh()}catch(e){show('Tune stop error: '+e)}};\n"
"async function stopMic(){if(txTimer)clearInterval(txTimer);txTimer=null;if(txNode){txNode.disconnect();txNode.onaudioprocess=null}txNode=null;if(txStream)for(const t of txStream.getTracks())t.stop();txStream=null;if(txContext)await txContext.close();txContext=null;try{await Promise.race([txUpload,new Promise(resolve=>setTimeout(resolve,40))])}catch(e){}if(txLease!==null&&txKeyed){try{await api('/v1/tx/ptt/stop','PUT',txLease)}catch(e){show('Mic unkey error: '+e)}}txKeyed=false;if(txLease!==null){try{await api('/v1/tx/sessions/current','DELETE',txLease)}catch(e){}}txLease=null}\n"
"document.querySelector('#micStop').onclick=async()=>{await stopMic();show('Computer microphone TX stopped');await refresh()};\n";

static const char page_part4b[] =
"document.querySelector('#micStart').onclick=async()=>{try{if(!window.isSecureContext||!navigator.mediaDevices)throw new Error('Microphone capture requires localhost or HTTPS');await stopMic();await stopAudio();const mode=document.querySelector('#mode').value.toLowerCase();if(mode!=='am'&&mode!=='usb'&&mode!=='lsb')throw new Error('Computer microphone TX supports AM, USB, or LSB only');await applyTxSettings();const drive=Number(document.querySelector('#txdrive').value);const j=await api('/v1/tx/sessions','POST',null,{mode,source:'audio',drive_percent:drive,sample_rate:48000,sample_format:'s16le',channels:1});txLease=j.lease;txStream=await navigator.mediaDevices.getUserMedia({audio:{channelCount:1,echoCancellation:false,noiseSuppression:false,autoGainControl:false}});txContext=new AudioContext({sampleRate:48000});if(txContext.sampleRate!==48000)throw new Error('Browser microphone rate is '+txContext.sampleRate+', expected 48000');const source=txContext.createMediaStreamSource(txStream);txNode=txContext.createScriptProcessor(4096,1,1);const silent=txContext.createGain();silent.gain.value=0;let firstChunkResolve,firstChunkReject;const firstChunk=new Promise((resolve,reject)=>{firstChunkResolve=resolve;firstChunkReject=reject});source.connect(txNode);txNode.connect(silent);silent.connect(txContext.destination);txNode.onaudioprocess=e=>{if(txLease===null)return;const f=e.inputBuffer.getChannelData(0),b=new ArrayBuffer(f.length*2),v=new DataView(b);for(let n=0;n<f.length;n++)v.setInt16(n*2,Math.max(-32768,Math.min(32767,Math.round(f[n]*32767))),true);txUpload=txUpload.then(()=>api('/v1/tx/audio','POST',txLease,b)).then(()=>{if(firstChunkResolve){firstChunkResolve();firstChunkResolve=null;firstChunkReject=null}}).catch(error=>{if(firstChunkReject){firstChunkReject(error);firstChunkResolve=null;firstChunkReject=null}show('Mic upload error: '+error);setTimeout(()=>stopMic(),0)})};await txContext.resume();await Promise.race([firstChunk,new Promise((_,reject)=>setTimeout(()=>reject(new Error('Microphone produced no audio frames')),2000))]);await api('/v1/tx/ptt/start','PUT',txLease);txKeyed=true;txTimer=setInterval(()=>api('/v1/tx/sessions/keepalive','PUT',txLease).catch(e=>show('TX keepalive error: '+e)),5000);show('Computer microphone transmitting: '+mode.toUpperCase()+' '+drive+'%')}catch(e){show('Mic TX start error: '+e);await stopMic()}};\n"
"const micSecurity=document.querySelector('#micSecurity');if(!window.isSecureContext||!navigator.mediaDevices){micSecurity.textContent='Computer microphone TX is disabled here: open this page from localhost or HTTPS.';document.querySelector('#micStart').disabled=true}else{micSecurity.textContent='Computer microphone access is available; your browser will request permission when TX starts.'}\n";

static const char page_part4c[] =
"function receiveOnly(){controlOwner=false;controlLease=null;if(controlTimer)clearInterval(controlTimer);controlTimer=null;for(const id of ['gain','tuneStart','tuneStop','micStart','micStop','txdrive','txmicgain','txtimeout','txcompressor'])document.querySelector('#'+id).disabled=true}\n"
"async function acquireControl(){const r=await fetch('/v1/control/owner',{method:'POST'}),j=await r.json();if(r.status===201){controlLease=j.lease;controlOwner=true;controlTimer=setInterval(()=>api('/v1/control/owner/keepalive').catch(()=>{receiveOnly();show('Station-control lease lost; this browser is now receive-only')}),5000);show('This browser owns station control')}else if(r.status===409){receiveOnly();show('Receive-only: another station owns hardware and TX')}else if(r.status===404){controlOwner=true;show('Receive-only daemon: hardware RX controls available')}else throw new Error(JSON.stringify(j))}\n"
"addEventListener('pagehide',()=>{if(controlLease!==null)fetch('/v1/control/owner',{method:'DELETE',headers:{'X-Flex1500-Control-Lease':String(controlLease)},keepalive:true}).catch(()=>{})});\n"
"document.querySelector('#tune').onclick=async()=>{const hz=Number(document.querySelector('#frequency').value);try{if(controlOwner){await api('/v1/radio/frequency/'+hz);hardwareHz=hz;virtualOffsetHz=0;show('Hardware tuned to '+hz)}else{const r=await fetch('/v1/radio'),j=await r.json();hardwareHz=j.frequency_hz;if(Math.abs(hz-hardwareHz)>24000)throw new Error('Outside owner IQ window '+(hardwareHz-24000)+'..'+(hardwareHz+24000));virtualOffsetHz=hardwareHz-hz;show('Receive-only virtual tune '+hz+' Hz; hardware remains '+hardwareHz+' Hz')}}catch(e){show('Tune error: '+e)}};\n"
"refresh().then(acquireControl).catch(e=>show('Ownership error: '+e));\n"
"</script></body></html>\n";

const char *flex1500_web_ui(size_t *length)
{
    static char page[sizeof(page_part1a) + sizeof(page_part1b) +
                     sizeof(page_part2) + sizeof(page_part2b) +
                     sizeof(page_part3) +
                     sizeof(page_part4a) + sizeof(page_part4b) +
                     sizeof(page_part4c) - 7];
    static int initialized;
    if (!initialized) {
        memcpy(page, page_part1a, sizeof(page_part1a) - 1);
        size_t offset = sizeof(page_part1a) - 1;
        memcpy(page + offset, page_part1b, sizeof(page_part1b) - 1);
        offset += sizeof(page_part1b) - 1;
        memcpy(page + offset, page_part2, sizeof(page_part2) - 1);
        offset += sizeof(page_part2) - 1;
        memcpy(page + offset, page_part2b, sizeof(page_part2b) - 1);
        offset += sizeof(page_part2b) - 1;
        memcpy(page + offset, page_part3, sizeof(page_part3) - 1);
        offset += sizeof(page_part3) - 1;
        memcpy(page + offset, page_part4a, sizeof(page_part4a) - 1);
        offset += sizeof(page_part4a) - 1;
        memcpy(page + offset, page_part4b, sizeof(page_part4b));
        offset += sizeof(page_part4b) - 1;
        memcpy(page + offset, page_part4c, sizeof(page_part4c));
        initialized = 1;
    }
    if (length != NULL) *length = sizeof(page) - 1;
    return page;
}
