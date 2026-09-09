const $ = (selector) => document.querySelector(selector);
const icon = (name, cls = '') => `<svg class="${cls}" aria-hidden="true"><use href="#${name}"/></svg>`;
const defaults = {appearance:'深色',accent:0,floating:true,opacity:38,three:true,performance:false,layout:'grid',sort:'最近游玩',renderer:'Metal 原生',background:'游戏封面'};
let settings;
try { settings = {...defaults,...JSON.parse(localStorage.getItem('mikage.preview') || '{}')}; } catch { settings = {...defaults}; }
let page = 'library', query = '', auto = false, mouse = false, started = 0, timer, toastTimer;
const colors = ['#9ca5b8','#d9c199','#88a5d1'];
const systemTheme = matchMedia('(prefers-color-scheme: light)');
function applySettings(){
  document.body.classList.toggle('light',settings.appearance === '浅色' || settings.appearance === '跟随系统' && systemTheme.matches);
  document.documentElement.style.setProperty('--accent',colors[settings.accent] || colors[0]);
  $('.performance').hidden = !settings.performance;
  $('.floating').style.opacity = settings.opacity/100;
  $('.floating').innerHTML = icon(settings.floating ? 'paw' : 'list');
  $('#player').style.backgroundImage = settings.background === '纯黑' ? 'none' : '';
  try { localStorage.setItem('mikage.preview',JSON.stringify(settings)); } catch { }
}
function render(){
  applySettings();
  document.querySelectorAll('nav button').forEach(b => {b.classList.toggle('selected',b.dataset.action===page);b.setAttribute('aria-current',b.dataset.action===page?'page':'false')});
  if(page==='library') renderLibrary(); else renderSettings();
}
function renderLibrary(){
  $('#content').innerHTML = `<header class="library-header"><div><h1>游戏库</h1><div class="subtitle">1部作品 · 共488.4 MB</div></div><div class="tools"><div class="layout-switch"><button data-action="grid" aria-label="网格视图" class="${settings.layout==='grid'?'selected':''}">${icon('grid')}</button><button data-action="list" aria-label="列表视图" class="${settings.layout==='list'?'selected':''}">${icon('list')}</button></div><button class="round" data-action="sort" aria-label="排序">${icon('sort')}</button><button class="round" data-action="import" aria-label="添加游戏">${icon('plus')}</button></div></header><div class="search">${icon('search')}<input type="search" placeholder="搜索游戏" aria-label="搜索游戏"><button data-action="clear" aria-label="清除搜索" hidden>×</button></div><div id="library-results"></div>`;
  $('.search input').value = query;
  $('.search input').addEventListener('input',e=>{query=e.target.value;renderResults()});
  renderResults();
}
function renderResults(){
  $('[data-action=clear]').hidden = !query;
  if(query && !'青空下的加缪'.includes(query.toLowerCase())) {$('#library-results').innerHTML='<div class="empty"><strong>没有找到游戏</strong>试试其他名称</div>';return;}
  $('#library-results').innerHTML = `${!query?`<button class="continue" data-action="player" aria-label="继续上次：青空下的加缪"><span class="continue-label">继续上次</span><span class="continue-copy"><strong>青空下的加缪</strong><span>7分钟前</span></span><span class="play-circle">${icon('play')}</span></button>`:''}<div class="game-grid ${settings.layout==='list'?'list':''}"><button class="game-card" data-action="player" aria-label="运行 青空下的加缪"><div class="card-art"><span>7分钟前</span></div><div class="card-copy"><strong>青空下的加缪</strong><span>488.4 MB</span></div></button></div>`;
}
function row(title,symbol,end,action='') {return `<${action?'button':'div'} class="setting-row" ${action?`data-action="${action}"`:''}><span class="setting-icon">${icon(symbol)}</span><span class="setting-name">${title}</span><span class="setting-end">${end}</span></${action?'button':'div'}>`;}
const chevron = icon('chevron','chevron');
function toggle(title,key){return `<button class="toggle" role="switch" aria-label="${title}" aria-checked="${settings[key]}" data-toggle="${key}"></button>`;}
function group(title,body,footer=''){return `<section class="settings-section"><div class="section-label">${title}</div><div class="settings-group">${body}</div>${footer?`<p class="section-footer">${footer}</p>`:''}</section>`;}
function renderSettings(){
  $('#content').innerHTML = `<h1 class="settings-title">设置</h1>`+
  group('外观',row('外观','half','')+`<div class="appearance-switch" aria-label="外观">${['跟随系统','浅色','深色'].map(v=>`<button data-appearance="${v}" aria-pressed="${settings.appearance===v}" class="${settings.appearance===v?'selected':''}">${v}</button>`).join('')}</div>`+row('点缀色','palette',`<span class="accent-options">${colors.map((c,i)=>`<button class="accent-dot ${settings.accent===i?'selected':''}" style="--dot:${c}" data-accent="${i}" aria-label="${['雾灰','麦金','晴蓝'][i]}" aria-pressed="${settings.accent===i}"></button>`).join('')}</span>`)+row('App 图标','app',`<span class="mini-icon">${icon('books')}</span>${chevron}`,'app-icon'))+
  group('图形',row('渲染方式','cpu',settings.renderer+chevron,'renderer'),'游戏显示异常时可尝试切换')+
  group('游戏内',row('背景填充','image',settings.background+chevron,'background')+row('悬浮球','paw',toggle('悬浮球','floating'))+row('闲置透明度','half',`<input type="range" min="10" max="100" value="${settings.opacity}" aria-label="闲置透明度"><output>${settings.opacity}%</output>`)+row('三指轻点唤出菜单','hand',toggle('三指轻点唤出菜单','three'))+row('性能分析','gauge',toggle('性能分析','performance')))+
  group('文件与传输',row('局域网上传','wifi','待接入'+chevron,'wifi')+row('存储信息','drive',chevron,'storage'))+
  group('更多',row('问题反馈','mail',chevron,'feedback')+row('关于','info',chevron,'about'))+`<p class="version">MIKAGE NEXT · 0.1</p>`;
  $('input[type=range]').addEventListener('input',e=>{settings.opacity=Number(e.target.value);$('output').textContent=settings.opacity+'%';applySettings()});
}
function modal(title,body){$('#dialog-title').textContent=title;$('#dialog-body').textContent=body;$('#dialog').showModal();}
function options(title,key,values){modal(title,'');$('#dialog-body').innerHTML=values.map(v=>`<button class="option" data-option="${v}" data-key="${key}">${v}${settings[key]===v?'　✓':''}</button>`).join('');}
function toast(text){$('#toast').textContent=text;$('#toast').classList.add('visible');clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('#toast').classList.remove('visible'),2500);}
function renderMenu(){
  $('.menu-actions').innerHTML = [['game-menu','list','菜单'],['auto','auto','自动'],['mouse','mouse','鼠标'],['capture','camera','截图'],['exit','exit','退出']].map(([action,symbol,title])=>`<button class="menu-action ${action==='exit'?'danger':''} ${action==='auto'&&auto || action==='mouse'&&mouse?'active':''}" data-action="${action}">${icon(symbol)}<span>${title}</span></button>`).join('');
  $('.cursor').hidden = !mouse;
}
function openPlayer(){
  $('#player').hidden=false;$('#menu-shade').hidden=false;document.body.style.overflow='hidden';
  started=Date.now();auto=false;mouse=false;renderMenu();updateTime();clearInterval(timer);timer=setInterval(updateTime,1000);
}
function updateTime(){const secs=Math.floor((Date.now()-started)/1000);$('#elapsed').textContent=`${Math.floor(secs/3600)}:${String(Math.floor(secs/60)%60).padStart(2,'0')}:${String(secs%60).padStart(2,'0')}`;}
function exitPlayer(){clearInterval(timer);$('#player').hidden=true;document.body.style.overflow='';}
document.addEventListener('click',e=>{
  const b=e.target.closest('button');if(!b)return;
  if(b.dataset.toggle){settings[b.dataset.toggle]=!settings[b.dataset.toggle];render();return;}
  if(b.dataset.appearance){settings.appearance=b.dataset.appearance;render();return;}
  if(b.dataset.accent!==undefined){settings.accent=Number(b.dataset.accent);render();return;}
  if(b.dataset.option){settings[b.dataset.key]=b.dataset.option;$('#dialog').close();render();return;}
  switch(b.dataset.action){
    case 'library':case 'settings':page=b.dataset.action;render();window.scrollTo(0,0);break;
    case 'grid':case 'list':settings.layout=b.dataset.action;render();break;
    case 'clear':query='';renderLibrary();$('.search input').focus();break;
    case 'sort':options('排序方式','sort',['最近游玩','添加时间','名称','大小']);break;
    case 'renderer':options('渲染方式 · 偏好预览','renderer',['Metal 原生','OpenGL ES']);break;
    case 'background':options('背景填充','background',['游戏封面','纯黑']);break;
    case 'import':modal('添加游戏','网页版仅用于核对界面。\n\n原生 iOS 工程已实现游戏文件夹导入、自定义封面和本地游戏库保存。ZIP 与 Wi-Fi 传输待接入。');break;
    case 'wifi':modal('局域网上传','传输模块尚未连接，预览不会启动上传服务。');break;
    case 'storage':modal('存储信息','演示作品：1 部\n演示大小：488.4 MB\n\n这是截图布局使用的示例数据，浏览器没有存储任何游戏文件。');break;
    case 'app-icon':modal('App 图标','当前使用默认图标示意，替换图标资源将在后续版本加入。');break;
    case 'feedback':modal('问题反馈','请在当前任务中反馈你希望调整的布局、交互或颜色。原生应用提供系统分享形式的问题模板。');break;
    case 'about':modal('Mikage Next','0.1 · 界面与导入原型\n\n按参考截图复刻游戏库、设置和横屏游戏菜单。\n\n当前没有连接 KRKR 运行时。这里展示的封面是原创矢量示意，不包含游戏内容。');break;
    case 'dismiss':$('#dialog').close();break;
    case 'player':openPlayer();break;
    case 'menu':$('#menu-shade').hidden=false;break;
    case 'close-menu':$('#menu-shade').hidden=true;break;
    case 'game-menu':modal('游戏菜单','真实游戏菜单将由 KRKR 引擎提供。此处展示系统菜单的布局。');break;
    case 'auto':auto=!auto;renderMenu();toast(auto?'自动模式已开启（界面演示）':'自动模式已关闭');break;
    case 'mouse':mouse=!mouse;renderMenu();break;
    case 'capture':modal('截图','原生 iOS 演示可以生成画面并通过系统分享导出。网页预览未接入游戏截图。');break;
    case 'exit':modal('退出演示','演示不会产生存档。');$('#dialog-body').insertAdjacentHTML('beforeend','<button class="option" data-action="confirm-exit">返回游戏库</button>');break;
    case 'confirm-exit':$('#dialog').close();exitPlayer();break;
    case 'reset':settings={...defaults};page='library';query='';exitPlayer();render();toast('预览已重置');break;
  }
});
document.addEventListener('keydown',e=>{if(e.key==='Escape'&&!$('#player').hidden&&!$('#dialog').open){$('#menu-shade').hidden=!$('#menu-shade').hidden;}});
document.addEventListener('touchstart',e=>{if(e.touches.length===3&&settings.three&&!$('#player').hidden){$('#menu-shade').hidden=false;}},{passive:true});
$('#dialog').addEventListener('click',e=>{if(e.target===$('#dialog')){const r=$('#dialog').getBoundingClientRect();if(e.clientX<r.left||e.clientX>r.right||e.clientY<r.top||e.clientY>r.bottom)$('#dialog').close();}});
systemTheme.addEventListener('change',applySettings);
render();
