#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify Android packaging rejects actual Godot script errors."""
import argparse
from contextlib import redirect_stdout
import importlib.util
import io
import shutil
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[3]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--godot', type=Path, default=ROOT / 'build-tools/godot-4.7.2/Godot_v4.7.2-stable_linux.x86_64')
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('android_builder', Path(__file__).with_name('build-android.py'))
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    (ROOT / 'out').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='android-script-check-', dir=ROOT / 'out') as directory:
        project = Path(directory)
        stage = project / 'app'
        stage.mkdir()
        previous = stage / 'libgrandleon_godot.so'
        previous.write_bytes(b'previous native build input')
        preserved = builder.preserve_native_inputs(stage, {'arm64-v8a': previous})
        shutil.rmtree(stage)
        assert preserved['arm64-v8a'].read_bytes() == b'previous native build input', 'restaging must preserve reused native inputs'
        (project / 'project.godot').write_text('config_version=5\n[application]\nrun/main_scene="res://main.tscn"\n')
        (project / 'main.tscn').write_text('[gd_scene load_steps=2 format=3]\n[ext_resource type="Script" path="res://main.gd" id="1"]\n[node name="Check" type="Node"]\nscript=ExtResource("1")\n')
        script = project / 'main.gd'
        script.write_text('extends Node\nfunc _ready():\n\tpass\n')
        with redirect_stdout(io.StringIO()):
            builder.run_godot(args.godot.resolve(), '--headless', '--editor', '--path', project, '--import', '--quit')
        script.write_text('extends Node\nfunc _ready(:\n\tpass\n')
        output = io.StringIO()
        try:
            with redirect_stdout(output):
                builder.run_godot(args.godot.resolve(), '--headless', '--editor', '--path', project, '--import', '--quit')
        except RuntimeError:
            assert 'SCRIPT ERROR:' in output.getvalue(), 'fixture must exercise a real GDScript parser failure'
        else:
            raise AssertionError('Android builder accepted a project with an actual script parse error')
    print('ANDROID_BUILD_SCRIPT_GUARD_PASS valid_import=true invalid_import_rejected=true staged_inputs_preserved=true')

if __name__ == '__main__':
    main()
