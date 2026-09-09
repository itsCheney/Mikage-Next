import http from 'node:http';
import {readFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
const root=fileURLToPath(new URL('../preview/',import.meta.url));
const allowed=new Map([['/','index.html'],['/index.html','index.html'],['/style.css','style.css'],['/app.js','app.js'],['/sky.svg','sky.svg']]);
const types={'.html':'text/html; charset=utf-8','.css':'text/css; charset=utf-8','.js':'text/javascript; charset=utf-8','.svg':'image/svg+xml'};
http.createServer(async(req,res)=>{
  const file=allowed.get(new URL(req.url,'http://127.0.0.1').pathname);
  if(!file){res.writeHead(404);res.end('Not found');return;}
  try{const data=await readFile(path.join(root,file));res.writeHead(200,{'Content-Type':types[path.extname(file)],'Cache-Control':'no-store'});res.end(data);}
  catch{res.writeHead(500);res.end('Unable to read preview');}
}).listen(4173,'127.0.0.1',()=>console.log('Mikage Next preview: http://127.0.0.1:4173'));
