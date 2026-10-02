// Bounded, no-model probe. Uses its own CODEX_HOME; never resumes a user session.
const {spawn,spawnSync}=require('node:child_process');
const fs=require('node:fs'),path=require('node:path'),readline=require('node:readline');
const exe=process.argv[2];if(!exe)throw Error('Pass absolute codex executable path');
const home=path.resolve(__dirname,'../../.probe-codex-'+Date.now());fs.mkdirSync(home);
const live=process.argv.includes('--live');const shared=process.argv.includes('--shared');let owner=null,endpoint=null;
const report={at:new Date().toISOString(),version:spawnSync(exe,['--version'],{encoding:'utf8'}).stdout.trim(),isolatedHome:!live,disposableWorkspace:home,modelTurnsStarted:0,results:[]};
class Client{
 constructor(){this.seq=1;this.pending=new Map();this.events=[];this.p=spawn(shared?'pwsh':exe,shared?['-NoProfile','-File',path.join(__dirname,'ws-stdio.ps1'),'-Url',endpoint]:['app-server'],{env:live?process.env:{...process.env,CODEX_HOME:home},stdio:['pipe','pipe','pipe']});this.p.stderr.on('data',()=>{});this.p.on('error',e=>this.fail(e));this.p.on('exit',()=>this.fail(Error('process exited')));readline.createInterface({input:this.p.stdout}).on('line',l=>{try{const m=JSON.parse(l);if(m.method){this.events.push(m.method);if(m.method==="turn/completed")this.terminal=m.params?.turn;if(m.method==="error")this.lastError=m.params?.error;return}const p=this.pending.get(m.id);if(p){clearTimeout(p.timer);this.pending.delete(m.id);m.error?p.reject(Error(JSON.stringify(m.error))):p.resolve(m.result)}}catch{}})}
 fail(e){for(const p of this.pending.values()){clearTimeout(p.timer);p.reject(e)}this.pending.clear()}
 call(method,params={}){return new Promise((resolve,reject)=>{const id=this.seq++;const timer=setTimeout(()=>{this.pending.delete(id);reject(Error('timeout '+method))},8000);this.pending.set(id,{resolve,reject,timer});this.p.stdin.write(JSON.stringify({id,method,params})+'\n')})}
 async init(){await this.call('initialize',{clientInfo:{name:'snowball-scope-probe',version:'0.1'}});this.p.stdin.write(JSON.stringify({method:'initialized',params:{}})+'\n')}
 close(){this.p.stdin.end();this.p.kill()}
}
async function check(name,fn){try{const detail=await fn();report.results.push({name,status:'pass',detail});return detail}catch(e){report.results.push({name,status:'failed',error:String(e.message).slice(0,800)});return null}}
(async()=>{if(shared){const net=require('node:net');const port=await new Promise(resolve=>{const srv=net.createServer();srv.listen(0,'127.0.0.1',()=>{const p=srv.address().port;srv.close(()=>resolve(p))})});endpoint='ws://127.0.0.1:'+port;owner=spawn(exe,['app-server','--listen',endpoint],{stdio:['ignore','ignore','ignore']});await new Promise(r=>setTimeout(r,1500));}const a=new Client(),b=new Client();try{
 await check('A initialization',()=>a.init());await check('B initialization',()=>b.init());
 await check('model/list',async()=>{const r=await a.call('model/list');return {count:r.data?.length,hasEfforts:r.data?.some(x=>Array.isArray(x.supportedReasoningEfforts)),models:r.data?.map(x=>x.model)}});
 const t=await check('A thread/start without model turn',async()=>{const r=await a.call('thread/start',{cwd:home,approvalPolicy:'never',sandbox:'read-only',model:process.argv.includes('--catalog-model')?report.results.find(x=>x.name==='model/list')?.detail?.models?.[0]:undefined});if(!r.thread?.id)throw Error('missing thread id');return {id:r.thread.id}});
 if(t){await check('A own addConversationListener',async()=>{const r=await a.call('addConversationListener',{conversationId:t.id,experimentalRawEvents:false});if(!r.subscriptionId)throw Error('missing subscriptionId');return {subscriptionAccepted:true}});
 if(live)await check('A start disposable live turn',async()=>{const r=await a.call('turn/start',{threadId:t.id,input:[{type:'text',text:'This is a protocol transport test. Do not call tools, read files, or change anything. Write a short paragraph explaining what a rainbow is.'}]});report.modelTurnsStarted++;return {turnId:r.turn?.id,status:r.turn?.status}});
 if((process.argv.includes('--rejoin')||shared))await check('B native thread/resume rejoin disposable thread',async()=>{const r=await b.call('thread/resume',{threadId:t.id});return {sameThread:r.thread?.id===t.id,turns:r.thread?.turns?.map(x=>({id:x.id,status:x.status})),status:r.thread?.status}});
 await check('B cross-process addConversationListener',async()=>{const r=await b.call('addConversationListener',{conversationId:t.id,experimentalRawEvents:false});if(!r.subscriptionId)throw Error('missing subscriptionId');return {subscriptionAccepted:true}});
 await check('B thread/list query (visibility observation)',async()=>{const r=await b.call('thread/list',{limit:100,cwd:home});return {count:r.data?.length,containsA:r.data?.some(x=>x.id===t.id)}});
 await check('B thread/read',async()=>{const r=await b.call('thread/read',{threadId:t.id,includeTurns:true});return {hasThread:!!r.thread}});
 if(live){const until=Date.now()+40000;while(Date.now()<until&&!a.events.includes('turn/completed'))await new Promise(r=>setTimeout(r,250));report.results.push({name:'A live turn terminal event',status:a.events.includes('turn/completed')?'observed':'timeout'});if(shared&&a.terminal?.status==='completed'){
 await check('B rejoin after first completed turn',async()=>{const r=await b.call('thread/resume',{threadId:t.id});return {sameThread:r.thread?.id===t.id}});
 const beforeA=a.events.length,beforeB=b.events.length;
 a.terminal=null;b.terminal=null;
 const second=await check('A second disposable turn',async()=>{const r=await a.call('turn/start',{threadId:t.id,input:[{type:'text',text:'No tools or file access. Write a 250 word description of clouds for this streaming test.'}]});report.modelTurnsStarted++;return {turnId:r.turn?.id}});
 const stopAt=Date.now()+30000;while(Date.now()<stopAt&&!b.events.slice(beforeB).includes('item/agentMessage/delta')&&!a.terminal)await new Promise(r=>setTimeout(r,100));
 if(second?.turnId&&!a.terminal)await check('B interrupts same-owner active turn',()=>b.call('turn/interrupt',{threadId:t.id,turnId:second.turnId}));
 const end=Date.now()+8000;while(Date.now()<end&&!a.terminal)await new Promise(r=>setTimeout(r,100));
 report.sharedSecondTurn={ownerDeltas:a.events.slice(beforeA).filter(x=>x==='item/agentMessage/delta').length,observerDeltas:b.events.slice(beforeB).filter(x=>x==='item/agentMessage/delta').length,ownerStatus:a.terminal?.status,observerStatus:b.terminal?.status};
}}
 }
 }finally{a.close();b.close();if(owner)owner.kill();report.sharedOwner=shared;report.events={A:a.events,B:b.events};report.terminal={status:a.terminal?.status,error:a.terminal?.error||a.lastError};report.observerTerminal={status:b.terminal?.status};fs.writeFileSync(path.resolve(__dirname,'../../docs/codex-scope-'+report.version.replace(/[^0-9.]/g,'')+(shared?'-shared':process.argv.includes('--rejoin')?'-rejoin':'')+(live?(process.argv.includes('--catalog-model')?'-catalog-live':'-live'):'')+'-result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2))}})().catch(e=>{console.error(e.message);process.exitCode=1});






