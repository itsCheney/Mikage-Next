import ast
import subprocess
from pathlib import Path

root=Path('D:/vn-sim')
line=(root/'build/native-metal-p2c-c1-baseline/production-syntax.log').read_text(encoding='utf-8').splitlines()[0]
base=ast.literal_eval(line[len('COMMAND '):])[:-1]
base+=['-I'+str(root/'build/metal-layer-tests/plutovg-patched/include')]
files=['core/script/tjsNativeLayer.cpp','plugins/LayerExBase.cpp','plugins/LayerExDraw/LayerExDraw.cpp']
lines=[]
for name in files:
    cmd=base+['Engine/KRKRRuntime/Source/cpp/'+name]
    result=subprocess.run(cmd,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    lines += ['COMMAND '+repr(cmd),result.stdout.decode('utf-8',errors='replace'),'EXIT '+str(result.returncode)]
    (root/'build/native-metal-p2c-c2b-baseline/production-syntax.log').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print(name,result.returncode,flush=True)
    if result.returncode:
        print(result.stdout.decode('utf-8',errors='replace'));raise SystemExit(result.returncode)
