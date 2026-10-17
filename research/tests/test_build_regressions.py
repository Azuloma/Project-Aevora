#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check cache policy, registry declarations, and both Sirit callback contracts."""
from pathlib import Path
import ast, os, re, shlex, shutil, subprocess, sys, tempfile, unittest
ROOT=Path(__file__).resolve().parents[2]
CXX=shlex.split(os.environ.get('CXX','c++'))


def run(args, cwd=None, success=True):
    p=subprocess.run(list(map(str,args)),cwd=cwd,capture_output=True,text=True,timeout=90)
    if (p.returncode==0) != success:
        raise AssertionError(f'{args}\nexit={p.returncode}\n{p.stdout}\n{p.stderr}')
    return p

class BuildRegressions(unittest.TestCase):
    def test_new_memory_fixes_invalidate_earlier_abi5_export_caches(self):
        source=(ROOT/'src/suyu/game_export.cpp').read_text()
        self.assertEqual(source.count('20260920-fixedpoint-v1'),2)
        self.assertIn('same_scan && same_backend && same_image_abi && same_correctness_revision',source)
        self.assertIn('const bool same_correctness_revision = contents.contains(',source)
        self.assertIn('same_translate_all && same_source && same_fallback_policy &&',source)
        self.assertIn('"source_exefs_sha256"',source.replace('\\"', '"'))

    @unittest.skipUnless(os.name == 'nt', 'requires retained Windows Qt/MSVC toolchain')
    def test_synthetic_export_exefs_replacement(self):
        run([sys.executable, ROOT/'tests/export_regression/run_fixture.py'])

    def test_generated_registry_declares_public_c_entry_points(self):
        source=(ROOT/'src/suyu/game_export.cpp').read_text()
        begin=source.index('"} SuyuRecompStaticModule;\\n\\n"')
        end=source.index('"static const SuyuRecompStaticModule s_modules[]',begin)
        emitted=''.join(ast.literal_eval(s) for s in re.findall(r'"(?:[^"\\\n]|\\.)*"',source[begin:end]))
        required=[
            'const SuyuRecompStaticModule* suyu_recomp_static_modules_v4(unsigned* count);\n',
            'int suyu_recomp_static_guard_v2(unsigned version);\n',
        ]
        for declaration in required:self.assertIn(declaration,emitted)
        declarations=''.join(required)
        prefix='typedef struct { const char* name; } SuyuRecompStaticModule;\n'
        body=r"""
static const SuyuRecompStaticModule modules[]={{"test"}};
const SuyuRecompStaticModule* suyu_recomp_static_modules_v4(unsigned* count){*count=1;return modules;}
int suyu_recomp_static_guard_v2(unsigned version){return version==2;}
int main(void){unsigned count=0;return !(suyu_recomp_static_modules_v4(&count)&&count==1&&suyu_recomp_static_guard_v2(2));}
"""
        with tempfile.TemporaryDirectory(prefix='suyu-registry-declarations-') as tmp:
            d=Path(tmp); f=d/'registry.c'; exe=d/'registry'
            cc=shlex.split(os.environ.get('CC','cc'))
            cmd=cc+['-std=c11','-Wall','-Wextra','-Werror=missing-prototypes',f,'-o',exe]
            f.write_text(prefix+declarations+body);run(cmd);run([exe])
            f.write_text(prefix+body)
            result=run(cmd,success=False)
            self.assertRegex(result.stderr.lower(), r'prototype|declaration')

    def test_phi_values_and_parent_labels_for_both_sirit_apis(self):
        text=(ROOT/'src/shader_recompiler/backend/spirv/emit_spirv.cpp').read_text()
        callback=text[text.index('struct DeferredPhiPatch {'):text.index('\nvoid PatchPhiNodes(')]
        body=r'''
#include <cassert>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>
using Id=unsigned;
namespace IR { struct Inst { std::vector<unsigned> args; unsigned Arg(size_t i)const{return args.at(i);} }; }
struct EmitContext { Id Def(unsigned n)const{return n+100;} };
'''+callback+r'''
int main(){
 EmitContext ctx; IR::Inst a{{1,2}}, b{{3,4,5}};
 std::vector<IR::Inst*> instructions{&a,&b};
 std::function<Id(size_t)> old_api=DeferredPhiPatch{ctx,instructions};
 assert(old_api(0)==101 && old_api(1)==102);
 assert(old_api(0)==103 && old_api(1)==104 && old_api(2)==105);
 std::function<std::pair<Id,Id>(size_t,Id)> new_api=DeferredPhiPatch{ctx,instructions};
 assert((new_api(0,11)==std::pair<Id,Id>{101,11}));
 assert((new_api(1,22)==std::pair<Id,Id>{102,22}));
 assert((new_api(0,33)==std::pair<Id,Id>{103,33}));
 assert((new_api(1,44)==std::pair<Id,Id>{104,44}));
 assert((new_api(2,55)==std::pair<Id,Id>{105,55}));
}
'''
        with tempfile.TemporaryDirectory(prefix='suyu-phi-') as tmp:
            p=Path(tmp);(p/'test.cpp').write_text(body)
            run(CXX+['-std=c++20','-O1','-Wall','-Wextra','-Werror',p/'test.cpp','-o',p/'test'])
            run([p/'test'])

if __name__=='__main__': unittest.main(verbosity=2)
