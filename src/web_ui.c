// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/web_ui.h"

#include <string.h>

static const char page_part1[] =
"<!doctype html>\n"
"<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
"<title>flex1500d RX test</title><style>\n"
":root{color-scheme:dark;background:#10151b;color:#e7edf4;font:16px system-ui,sans-serif}\n"
"body{max-width:760px;margin:3rem auto;padding:0 1rem}fieldset{border:1px solid #405064;border-radius:.6rem;padding:1rem}\n"
"label{display:block;margin:.7rem 0}.row{display:flex;gap:.6rem;flex-wrap:wrap;align-items:end}\n"
"input,select,button{font:inherit;padding:.55rem .7rem;border-radius:.35rem;border:1px solid #60758c}\n"
"input,select{background:#17212b;color:inherit}input{width:12rem}button{background:#254b70;color:white;cursor:pointer}\n"
"button.stop{background:#713b3b}pre{white-space:pre-wrap;background:#17212b;padding:1rem;border-radius:.5rem;min-height:5rem}\n"
".safe{color:#8fd19e}.warn{color:#ffd479}</style></head><body>\n"
"<h1>FLEX-1500 receive test</h1>\n"
"<p class=\"safe\">Receive only. This page contains no TX or PTT control.</p>\n"
"<fieldset><legend>Radio</legend><div class=\"row\">\n"
"<label>Frequency (Hz)<br><input id=\"frequency\" type=\"number\" min=\"100000\" max=\"54000000\" step=\"1\" value=\"10000000\"></label>\n"
"<label>Mode<br><select id=\"mode\"><option>AM</option><option>USB</option><option>LSB</option><option>FM</option><option>CW</option></select></label>\n"
"<label>DSP bandwidth (Hz)<br><input id=\"bandwidth\" type=\"number\" min=\"100\" max=\"20000\" step=\"100\" value=\"6000\"></label>\n"
"<label>Squelch (dBFS)<br><input id=\"squelch\" type=\"number\" min=\"-120\" max=\"0\" step=\"1\" value=\"-120\"></label>\n"
"<label>RX gain<br><select id=\"gain\"><option value=\"-10\">-10 dB</option><option value=\"0\">0 dB</option><option value=\"10\">+10 dB</option><option value=\"20\" selected>+20 dB</option><option value=\"30\">+30 dB</option></select></label>\n"
"<button id=\"tune\">Tune RX</button><button id=\"refresh\">Refresh status</button></div>\n"
"<div class=\"row\"><button id=\"start\">Start audio</button><button class=\"stop\" id=\"stop\">Stop audio</button></div>\n"
"<p class=\"warn\">Changing mode or DSP bandwidth stops audio. Tuning remains continuous. CW uses a 700 Hz receive tone.</p></fieldset>\n"
"<h2>Status</h2><pre id=\"status\">Loading…</pre>\n"
"<script>\n"
"const statusBox=document.querySelector('#status');let aborter=null,context=null,underruns=0;\n"
"function show(v){statusBox.textContent=typeof v==='string'?v:JSON.stringify(v,null,2)}\n"
"async function refresh(){try{const r=await fetch('/v1/radio'),radio=await r.json();if(radio.frequency_hz)document.querySelector('#frequency').value=radio.frequency_hz;if(radio.rx_mode)document.querySelector('#mode').value=radio.rx_mode.toUpperCase();if(radio.rx_gain_db!==null)document.querySelector('#gain').value=radio.rx_gain_db;if(radio.rx_bandwidth_hz)document.querySelector('#bandwidth').value=radio.rx_bandwidth_hz;if(radio.rx_squelch_db!==undefined)document.querySelector('#squelch').value=radio.rx_squelch_db;const s=await fetch('/v1/status'),service=await s.json();show({radio,service})}catch(e){show('Status error: '+e)}}\n"
"async function stopAudio(){if(aborter)aborter.abort();aborter=null;if(context)await context.close();context=null}\n"
"document.querySelector('#refresh').onclick=refresh;document.querySelector('#stop').onclick=stopAudio;\n"
"document.querySelector('#tune').onclick=async()=>{const hz=document.querySelector('#frequency').value;try{const r=await fetch('/v1/radio/frequency/'+hz,{method:'PUT'});const body=await r.json();if(!r.ok)throw new Error(JSON.stringify(body));show(body);await refresh()}catch(e){show('Tune error: '+e)}};\n";

static const char page_part2[] =
"document.querySelector('#mode').onchange=async()=>{await stopAudio();const mode=document.querySelector('#mode').value.toLowerCase();try{const r=await fetch('/v1/radio/mode/'+mode,{method:'PUT'});const body=await r.json();if(!r.ok)throw new Error(JSON.stringify(body));show(body);await refresh()}catch(e){show('Mode error: '+e)}};\n"
"document.querySelector('#gain').onchange=async()=>{const gain=document.querySelector('#gain').value;try{const r=await fetch('/v1/radio/gain/'+gain,{method:'PUT'});const body=await r.json();if(!r.ok)throw new Error(JSON.stringify(body));show(body);await refresh()}catch(e){show('Gain error: '+e)}};\n"
"for(const id of ['bandwidth','squelch'])document.querySelector('#'+id).onchange=async()=>{await stopAudio();const value=document.querySelector('#'+id).value;try{const r=await fetch('/v1/radio/'+id+'/'+value,{method:'PUT'});const body=await r.json();if(!r.ok)throw new Error(JSON.stringify(body));show(body);await refresh()}catch(e){show(id+' error: '+e)}};\n"
"document.querySelector('#start').onclick=async()=>{try{await stopAudio();\n"
"const mode=document.querySelector('#mode').value.toLowerCase();\n"
"const bandwidth=Number(document.querySelector('#bandwidth').value),squelch=Number(document.querySelector('#squelch').value);\n"
"context=new AudioContext({sampleRate:48000});if(context.sampleRate!==48000)throw new Error('Browser audio rate is '+context.sampleRate+', expected 48000');\n"
"const worklet=`class Q extends AudioWorkletProcessor{constructor(){super();this.q=[];this.o=0;this.buffered=0;this.playing=false;this.underruns=0;this.port.onmessage=e=>{const b=new Float32Array(e.data);this.q.push(b);this.buffered+=b.length;if(this.buffered>=4096)this.playing=true}}process(i,o){const a=o[0][0];a.fill(0);if(!this.playing)return true;let p=0;while(p<a.length&&this.q.length){const b=this.q[0],n=Math.min(a.length-p,b.length-this.o);a.set(b.subarray(this.o,this.o+n),p);p+=n;this.o+=n;this.buffered-=n;if(this.o===b.length){this.q.shift();this.o=0}}if(p<a.length){this.playing=false;this.underruns++;this.port.postMessage({underruns:this.underruns})}return true}}registerProcessor('f15-audio',Q)`;\n"
"const url=URL.createObjectURL(new Blob([worklet],{type:'application/javascript'}));await context.audioWorklet.addModule(url);URL.revokeObjectURL(url);\n"
"const node=new AudioWorkletNode(context,'f15-audio',{outputChannelCount:[1]});underruns=0;node.port.onmessage=e=>{if(e.data&&e.data.underruns!==undefined)underruns=e.data.underruns};node.connect(context.destination);await context.resume();\n"
"aborter=new AbortController();const response=await fetch('/v1/stream/iq',{signal:aborter.signal});if(!response.ok)throw new Error('IQ stream HTTP '+response.status);\n"
"const reader=response.body.getReader();let buffered=new Uint8Array(0),dc=0,lp=0,env=1,rfenv=1,phase=0,pi=0,pq=0,havePrev=false,settle=2400,frames=0;\n"
"const nt=129,hi=new Float32Array(nt),hq=new Float32Array(nt),ci=new Float32Array(nt),cq=new Float32Array(nt);let hp=0;if(mode==='usb'||mode==='lsb'||mode==='cw'){const low=mode==='cw'?Math.max(0,700-bandwidth/2):100,high=mode==='cw'?Math.min(12000,700+bandwidth/2):Math.min(12000,low+bandwidth),cutoff=(high-low)/2,center=(low+high)/2*(mode==='lsb'?-1:1),mid=(nt-1)/2;let sum=0;for(let t=0;t<nt;t++){const k=t-mid,w=.54-.46*Math.cos(2*Math.PI*t/(nt-1)),b=(k===0?2*cutoff/48000:Math.sin(2*Math.PI*cutoff*k/48000)/(Math.PI*k))*w;ci[t]=b*Math.cos(2*Math.PI*center*k/48000);cq[t]=b*Math.sin(2*Math.PI*center*k/48000);sum+=b}for(let t=0;t<nt;t++){ci[t]/=sum;cq[t]/=sum}}\n";

static const char page_part3[] =
"while(true){const {value,done}=await reader.read();if(done)break;const joined=new Uint8Array(buffered.length+value.length);joined.set(buffered);joined.set(value,buffered.length);buffered=joined;\n"
"while(buffered.length>=20){const v=new DataView(buffered.buffer,buffered.byteOffset);if(v.getUint32(0,false)!==0x46313549||v.getUint8(4)!==1||v.getUint8(5)!==1||v.getUint16(6,false)!==20||v.getUint32(12,false)!==48000)throw new Error('Invalid F15I frame');\n"
"const count=v.getUint32(16,false),length=20+count*8;if(buffered.length<length)break;const audio=new Float32Array(count);\n"
"for(let n=0;n<count;n++){const i=v.getFloat32(20+n*8,true),q=v.getFloat32(24+n*8,true);rfenv+=.002*(Math.hypot(i,q)-rfenv);let x=0;if(mode==='am'){x=Math.hypot(i,q)}else if(mode==='fm'){if(havePrev)x=Math.atan2(pi*q-pq*i,pi*i+pq*q);pi=i;pq=q;havePrev=true}else{hi[hp]=i;hq[hp]=q;let fi=0,fq=0,p=hp;for(let t=0;t<nt;t++){fi+=hi[p]*ci[t]-hq[p]*cq[t];fq+=hi[p]*cq[t]+hq[p]*ci[t];p=p===0?nt-1:p-1}hp=(hp+1)%nt;if(mode==='cw'){phase+=2*Math.PI*700/48000;x=fi*Math.cos(phase)-fq*Math.sin(phase)}else{x=fi}}if(phase>Math.PI)phase-=2*Math.PI;dc+=.0005*(x-dc);x-=dc;lp+=.25*(x-lp);env=Math.max(Math.abs(lp),env*.9995);const level=20*Math.log10((rfenv+1e-9)/32768);audio[n]=settle-->0||level<squelch?0:Math.max(-.8,Math.min(.8,.24*lp/(env+1e-6)))}\n"
"node.port.postMessage(audio.buffer,[audio.buffer]);frames++;buffered=buffered.slice(length);if(frames%100===0)show(mode.toUpperCase()+' audio running\\nF15I frames: '+frames+'\\nAudio rate: '+context.sampleRate+' Hz\\nAudio underruns: '+underruns)} }\n"
"}catch(e){if(e.name!=='AbortError')show('Audio error: '+e);await stopAudio()}};refresh();\n"
"</script></body></html>\n";

const char *flex1500_web_ui(size_t *length)
{
    static char page[sizeof(page_part1) + sizeof(page_part2) +
                     sizeof(page_part3) - 2];
    static int initialized;
    if (!initialized) {
        memcpy(page, page_part1, sizeof(page_part1) - 1);
        size_t offset = sizeof(page_part1) - 1;
        memcpy(page + offset, page_part2, sizeof(page_part2) - 1);
        offset += sizeof(page_part2) - 1;
        memcpy(page + offset, page_part3, sizeof(page_part3));
        initialized = 1;
    }
    if (length != NULL) *length = sizeof(page) - 1;
    return page;
}
