"""Validate private disc input, reproduce the pinned module image, optionally translate it."""
import argparse

if not __debug__:
    raise RuntimeError('Run this validation tool without Python optimization (-O).')
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
WC = ROOT / 'wiicompiled'
EXPECTED = 'b91ca3e670f6ac2d87050284c81404e2d3e708d1aba5a79b93e0ac9c6e4faafd'


def yaml_path(path):
    return "'" + str(path.resolve()).replace('\\', '/').replace("'", "''") + "'"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--disc', type=Path, required=True, help='Extracted DATA directory with sys/ and files/')
    p.add_argument('--decomp-config', type=Path, required=True, help='Public brawl decomp config/RSBE01_02')
    p.add_argument('--private-work', type=Path, required=True, help='Private generated directory outside this Git checkout')
    p.add_argument('--dotnet', type=Path, help='dotnet SDK capable of building net8.0; required unless --prepare-only')
    p.add_argument('--prepare-only', action='store_true')
    p.add_argument('--threads', type=int, choices=(1, 2), default=2)
    p.add_argument('--dotnet-roll-forward', choices=('Major', 'LatestMajor'), help='explicitly permit net8.0 CLI execution on a newer installed .NET runtime')
    args = p.parse_args()
    work = args.private_work.resolve()
    assert not work.is_relative_to(ROOT), 'Private source inputs must stay outside the Git checkout'
    assert args.threads > 0
    spec = importlib.util.spec_from_file_location('brawl_input_check', ROOT / 'tools/check-brawl-input.py')
    check = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(check)
    metadata = check.validate(args.disc.resolve(), WC / 'projects/brawl/module_layout.json')
    game, stage = work / 'game', work / 'module-stage'
    game.mkdir(parents=True, exist_ok=True)
    for rel in ('projects/brawl/gecko', 'runtime/src/hle'):
        (stage / rel).mkdir(parents=True, exist_ok=True)
    for rel in ('projects/brawl/MAP.txt', 'projects/brawl/gecko/codehandler.bin'):
        shutil.copyfile(WC / rel, stage / rel)
    source_dol = args.disc.resolve() / 'sys/main.dol'
    assert source_dol != game / 'main.dol', 'Input and generated destination overlap'
    shutil.copyfile(source_dol, game / 'main.dol')
    import sys
    subprocess.run([sys.executable, str(WC / 'projects/brawl/tools/build_modules.py'),
                    str(args.disc.resolve() / 'files/module'), str(args.decomp_config.resolve()),
                    str(game / 'brawl_modules.rel'), str(stage), str(game / 'main.dol')], check=True)
    metadata['main_translate_sha256'] = hashlib.sha256((game / 'main_translate.dol').read_bytes()).hexdigest()
    assert metadata['main_translate_sha256'] == EXPECTED, 'Prepared image differs from upstream compatibility pin'
    metadata['main_dol_verification'] = 'validated through the exact pinned prepared image; raw digest recorded'
    for rel in ('projects/brawl/module_layout.json', 'runtime/src/hle/brawl_module_layout.inc',
                'projects/brawl/resident_data_ranges.txt'):
        original, generated = (WC / rel).read_text(), (stage / rel).read_text()
        equal = json.loads(original) == json.loads(generated) if rel.endswith('.json') else original == generated
        assert equal, 'Reference metadata mismatch: ' + rel
    # The public decomp's latest function names may differ. Retain the exact tracked
    # MAP_full.txt used by this port; never replace its graph while generating input.
    config = (WC / 'projects/brawl/recomp.yml').read_text()
    config, count = re.subn(r'(?m)^workspace_root:.*$', lambda _: 'workspace_root: ' + yaml_path(WC), config)
    assert count == 1
    for name in ('main_translate.dol', 'brawl_modules.rel'):
        old = 'path: ../game/' + name
        assert config.count(old) == 1
        config = config.replace(old, 'path: ' + yaml_path(game / name))
    project = work / 'recomp-private.yml'
    project.write_text(config, encoding='utf-8')
    (work / 'compatibility.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(metadata, indent=2), flush=True)
    if args.prepare_only:
        print('Private inputs prepared; tracked maps unchanged. Project: ' + str(project))
        return
    assert args.dotnet and args.dotnet.is_file(), 'Pass --dotnet with a net8.0-capable SDK executable'
    cli = WC / 'translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll'
    subprocess.run([str(args.dotnet.resolve()), 'build', str(WC / 'translator/src/Translator.Cli/Translator.Cli.csproj'),
                    '-c', 'Release', '--nologo', '-maxcpucount:2', '-nodeReuse:false',
                    '-p:UseSharedCompilation=false'], check=True)
    environment = os.environ.copy()
    if args.dotnet_roll_forward:
        environment['DOTNET_ROLL_FORWARD'] = args.dotnet_roll_forward
    def translate(*parameters):
        subprocess.run([str(args.dotnet.resolve()), str(cli), *parameters], cwd=WC, env=environment, check=True)
    translate('translate-recursive', '0x8000403C', '--project', str(project), '--outdir', 'generated/functions',
              '--output-metadata', 'generated/base_translation_output.json', '--production-source-bundle',
              'generated/base_translation_sources.bin', '--no-function-files', '--no-state-free-abi',
              '--bounded-ir', '--data-in-text', 'projects/brawl/resident_data_ranges.txt',
              '--prune-stale', '--threads', str(args.threads))
    translate('generate-data-init', '--project', str(project))
    translate('emit-build-shards', '--project', str(project), '--base-metadata', 'generated/base_translation_output.json',
              '--base-functions-dir', 'generated/functions', '--native-source-dir', 'runtime/src', '--out',
              'generated/build_shards', '--base-shards', '600')


if __name__ == '__main__':
    main()
