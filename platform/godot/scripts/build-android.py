#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build a development Android APK containing the Grandleon player and creator."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import urllib.request
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
GODOT_VERSION = '4.7.2'
TEMPLATES_SHA256 = 'f298490b8d44d934be425a5a65a51bf15f422428b229a06a6e11d9ffea248011'
TEMPLATES_NAME = f'Godot_v{GODOT_VERSION}-stable_export_templates.tpz'
NDK_VERSION = '28.1.13356709'
BUILD_TOOLS = '36.1.0'
ABIS = {'arm64-v8a': 183, 'x86_64': 62}


def run(*args, env=None):
    subprocess.run([str(a) for a in args], cwd=ROOT, env=env, check=True)


def run_godot(*args, env=None):
    # Godot can exit successfully after reporting a GDScript parse error.
    # A signed APK is not usable if its application scripts failed to import.
    failed = False
    with subprocess.Popen([str(a) for a in args], cwd=ROOT, env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, bufsize=1) as process:
        for line in process.stdout:
            print(line, end='', flush=True)
            if re.search(r'SCRIPT ERROR:|(?:^|\s)ERROR:|Parse Error:', line):
                failed = True
        code = process.wait()
    if code:
        raise subprocess.CalledProcessError(code, args)
    if failed:
        raise RuntimeError('Godot reported script/import errors; refusing to package this application')


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def elf_info(path, abi):
    data = path.read_bytes()
    if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', data, 18)[0] != ABIS[abi]:
        raise ValueError(f'Wrong ELF architecture: {path}')
    offset = struct.unpack_from('<Q', data, 32)[0]
    stride, count = struct.unpack_from('<HH', data, 54)
    alignments = []
    for index in range(count):
        entry = offset + index * stride
        if struct.unpack_from('<I', data, entry)[0] == 1:
            alignment = struct.unpack_from('<Q', data, entry + 48)[0]
            if alignment < 16384:
                raise ValueError(f'Native library needs 16 KiB alignment: {path}')
            alignments.append(alignment)
    if not alignments:
        raise ValueError(f'ELF has no loadable segments: {path}')
    return {'sha256': digest(path), 'loadAlignments': alignments}


def templates(archive):
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        temporary = archive.with_suffix('.part')
        try:
            with urllib.request.urlopen(
                f'https://github.com/godotengine/godot-builds/releases/download/{GODOT_VERSION}-stable/{TEMPLATES_NAME}',
                timeout=120,
            ) as source, temporary.open('wb') as output:
                shutil.copyfileobj(source, output, 1024 * 1024)
            temporary.rename(archive)
        finally:
            temporary.unlink(missing_ok=True)
    if digest(archive) != TEMPLATES_SHA256:
        raise ValueError(f'Export template checksum mismatch: {archive}')
    destination = archive.parent / 'templates'
    destination.mkdir(exist_ok=True)
    with zipfile.ZipFile(archive) as source:
        for name in ['android_debug.apk', 'android_release.apk']:
            with source.open('templates/' + name) as contents, (destination / name).open('wb') as output:
                shutil.copyfileobj(contents, output)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / '.cache/android-sdk-local')))
    parser.add_argument('--java-home', type=Path, default=Path(os.environ.get('JAVA_HOME', '/usr/lib/jvm/java-17-openjdk-amd64')))
    parser.add_argument('--godot', type=Path, default=ROOT / f'build-tools/godot-{GODOT_VERSION}/Godot_v{GODOT_VERSION}-stable_linux.x86_64')
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-android')
    parser.add_argument('--host-build-dir', type=Path, default=ROOT / 'build-godot')
    parser.add_argument('--output', type=Path, default=ROOT / 'out/grandleon-android-development')
    parser.add_argument('--templates-archive', type=Path, default=ROOT / f'build-tools/godot-android-{GODOT_VERSION}' / TEMPLATES_NAME)
    parser.add_argument('--native-library', action='append', default=[], metavar='ABI=FILE', help='Reuse a prebuilt library for an ABI')
    parser.add_argument('--ui-test', action='store_true', help='Build a separate QA application that exercises real Android editor panels')
    parser.add_argument('--no-host-build', action='store_true', help='Reuse the host compiler and extension in --host-build-dir')
    args = parser.parse_args()
    sdk, java, build, host, output, godot = [p.resolve() for p in [args.sdk, args.java_home, args.build_dir, args.host_build_dir, args.output, args.godot]]
    if output.exists():
        parser.error(f'Output already exists: {output}')
    if not godot.is_file() or not (java / 'bin/java').is_file():
        parser.error('Provide the pinned Godot executable and a Java SDK with --godot/--java-home')
    ndk = sdk / 'ndk' / NDK_VERSION
    toolchain = ndk / 'build/cmake/android.toolchain.cmake'
    signer = sdk / 'build-tools' / BUILD_TOOLS / 'apksigner'
    if not toolchain.is_file() or not signer.is_file() or not (sdk / 'platforms/android-36/android.jar').is_file():
        parser.error(f'Install Android platform 36, build-tools {BUILD_TOOLS}, and NDK {NDK_VERSION} in --sdk')
    env = os.environ.copy()
    env.update(JAVA_HOME=str(java), ANDROID_HOME=str(sdk), ANDROID_SDK_ROOT=str(sdk))
    env['PATH'] = str(java / 'bin') + os.pathsep + env.get('PATH', '')
    cache = ROOT / 'build-godot/_deps/godot_cpp-src'
    binding_source = [f'-DFETCHCONTENT_SOURCE_DIR_GODOT_CPP={cache}'] if cache.is_dir() else []
    supplied = {}
    for option in args.native_library:
        abi, separator, path = option.partition('=')
        if not separator or abi not in ABIS or abi in supplied:
            parser.error('--native-library requires one ABI=FILE for a supported ABI')
        supplied[abi] = Path(path).resolve()
    libraries = {}
    for abi in ABIS:
        native = build / 'native' / abi
        if abi in supplied:
            libraries[abi] = supplied[abi]
            continue
        run('cmake', '-S', ROOT, '-B', native, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
            f'-DCMAKE_TOOLCHAIN_FILE={toolchain}', f'-DANDROID_ABI={abi}', '-DANDROID_PLATFORM=android-24',
            '-DANDROID_STL=c++_static', '-DGRANDLEON_BUILD_TESTS=OFF', '-DGRANDLEON_DESKTOP_SDL=OFF',
            '-DGRANDLEON_BUILD_GODOT=ON', '-DGRANDLEON_GODOT_API_VERSION=4.7', '-DGRANDLEON_WERROR=ON', *binding_source)
        run('cmake', '--build', native, '--parallel', '8', '--target', 'grandleon_godot')
        libraries[abi] = native / 'platform' / 'godot' / 'project' / 'bin' / 'android' / abi / 'libgrandleon_godot.so'
    native_proof = {abi: elf_info(path, abi) for abi, path in libraries.items()}
    readelf = ndk / 'toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf'
    for abi, library in libraries.items():
        symbols = subprocess.check_output([str(readelf), '--dyn-syms', '--dynamic', str(library)], text=True)
        if not re.search(r'GLOBAL\s+DEFAULT\s+\d+\s+grandleon_library_init\b', symbols):
            raise ValueError(f'Native extension entry point missing: {library}')
        dependencies = re.findall(r'\(NEEDED\).*\[([^]]+)\]', symbols)
        if set(dependencies) - {'libm.so', 'libdl.so', 'libc.so', 'liblog.so', 'libandroid.so'}:
            raise ValueError(f'Unbundled native dependency: {library}: {dependencies}')
        native_proof[abi]['dependencies'] = dependencies
    if not args.no_host_build:
        run('cmake', '-S', ROOT, '-B', host, '-DGRANDLEON_BUILD_GODOT=ON', '-DGRANDLEON_BUILD_TESTS=OFF',
            '-DGRANDLEON_DESKTOP_SDL=OFF', '-DGRANDLEON_GODOT_API_VERSION=4.7', '-DGRANDLEON_WERROR=ON', *binding_source)
        run('cmake', '--build', host, '--parallel', '8', '--target', 'grandleon_content_compile', 'grandleon_godot')
    host_library = host / 'platform' / 'godot' / 'project' / 'bin' / 'libgrandleon_godot.so'
    compiler = host / 'grandleon_content_compile'
    if not host_library.is_file() or not compiler.is_file():
        parser.error('Host compiler and extension missing; build without --no-host-build')
    template_dir = templates(args.templates_archive.resolve())
    build.mkdir(parents=True, exist_ok=True)
    stage = build / 'app'
    if output.is_relative_to(stage):
        parser.error('Output must be outside the staging directory')
    shutil.rmtree(stage, ignore_errors=True)
    shutil.copytree(ROOT / 'platform/godot/project', stage,
                    ignore=shutil.ignore_patterns('.godot', '*.so', '*.dll'))
    shutil.copytree(ROOT / 'tools/placeholder_art/assets/modern', stage / 'assets', dirs_exist_ok=True)
    shutil.copy2(host_library, stage / 'bin/libgrandleon_godot.so')
    for abi, library in libraries.items():
        destination = stage / 'bin/android' / abi
        destination.mkdir(parents=True, exist_ok=True)
        shutil.copy2(library, destination / library.name)
    shutil.copy2(ROOT / 'games/demo/source/project.json', stage / 'content/templates/starter_battle.json')
    run(compiler, ROOT / 'games/steel_and_cedar/source/project.json', stage / 'game.gpk')
    included = stage / 'content/included'
    included.mkdir(parents=True, exist_ok=True)
    shutil.copy2(stage / 'game.gpk', included / 'steel_and_cedar.gpk')
    licenses = stage / 'licenses'
    licenses.mkdir(exist_ok=True)
    for source, name in [
        ('LICENSE', 'GRANDLEON-LICENSE.txt'), ('NOTICE', 'GRANDLEON-NOTICE.txt'),
        ('third_party/godot/4.7.2/LICENSE.txt', 'GODOT-LICENSE.txt'),
        ('third_party/godot/4.7.2/COPYRIGHT.txt', 'GODOT-COPYRIGHT.txt'),
        ('third_party/godot-cpp/10.0.0-stable/LICENSE.md', 'GODOT-CPP-LICENSE.txt'),
        ('assets/audio/VSCO-2-CE-LICENSE.txt', 'VSCO-2-CE-LICENSE.txt'),
        ('assets/fonts/spleen/LICENSE', 'SPLEEN-LICENSE.txt'),
    ]:
        shutil.copy2(ROOT / source, licenses / name)
    notice = ndk / 'toolchains/llvm/prebuilt/linux-x86_64/NOTICE'
    shutil.copy2(notice, licenses / 'ANDROID-LLVM-RUNTIME-NOTICE.txt')
    # Android masks adaptive/splash icons to a circle. Pad the existing logo
    # into its safe area so the G and sword remain intact on every launcher.
    svg_namespace = 'http://www.w3.org/2000/svg'
    ET.register_namespace('', svg_namespace)
    icon = ET.fromstring((stage / 'icon.svg').read_text())
    origin_x, origin_y, width, height = map(float, icon.attrib['viewBox'].split())
    icon.set('viewBox', f'0 0 {width * 1.5:g} {height * 1.5:g}')
    group = ET.Element(f'{{{svg_namespace}}}g', {
        'transform': f'translate({width * 0.25 - origin_x:g} {height * 0.25 - origin_y:g})',
    })
    for child in list(icon):
        icon.remove(child)
        group.append(child)
    icon.append(group)
    (stage / 'android_icon.svg').write_text(ET.tostring(icon, encoding='unicode'))
    (stage / 'android_background.svg').write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 432 432">'
        '<rect width="432" height="432" fill="#07111b"/></svg>')
    if args.ui_test:
        shutil.copy2(ROOT / 'platform/godot/tests/android_ui_driver.gd', stage / 'android_ui_driver.gd')
        (stage / 'android_ui_driver.tscn').write_text(
            '[gd_scene load_steps=2 format=3]\n'
            '[ext_resource type="Script" path="res://android_ui_driver.gd" id="1"]\n'
            '[node name="AndroidUIChecks" type="Control"]\n'
            'layout_mode=3\nanchors_preset=15\nanchor_right=1.0\nanchor_bottom=1.0\n'
            'grow_horizontal=2\ngrow_vertical=2\nscript=ExtResource("1")\n')
    package_id = 'org.grandleon.app.qa' if args.ui_test else 'org.grandleon.app.dev'
    project = stage / 'project.godot'
    project.write_text(project.read_text().replace('config/name="Grandleon PC"', 'config/name="Grandleon"').replace(
        'renderer/rendering_method.mobile="gl_compatibility"',
        'renderer/rendering_method.mobile="gl_compatibility"\ntextures/vram_compression/import_etc2_astc=true',
    ))
    if args.ui_test:
        project.write_text(project.read_text().replace('run/main_scene="res://home.tscn"',
                                                     'run/main_scene="res://android_ui_driver.tscn"'))
    state = build / 'godot-state'
    for name in ['config', 'data', 'cache', 'tmp']:
        (state / name).mkdir(parents=True, exist_ok=True)
    env.update(XDG_CONFIG_HOME=str(state / 'config'), XDG_DATA_HOME=str(state / 'data'), XDG_CACHE_HOME=str(state / 'cache'))
    env.update(TMPDIR=str(state / 'tmp'), TEMP=str(state / 'tmp'), TMP=str(state / 'tmp'))
    java_tmp = str(state / 'tmp').replace('\\', '\\\\').replace('"', '\\"')
    env['JAVA_TOOL_OPTIONS'] = env.get('JAVA_TOOL_OPTIONS', '') + f' -Djava.io.tmpdir="{java_tmp}"'
    settings = state / f'config/godot/editor_settings-4.7.tres'
    settings.parent.mkdir(parents=True, exist_ok=True)
    keystore = build / 'debug.keystore'
    if not keystore.exists():
        run(java / 'bin/keytool', '-genkeypair', '-keystore', keystore, '-storepass', 'android',
            '-alias', 'androiddebugkey', '-keypass', 'android', '-dname', 'CN=Android Debug,O=Android,C=US',
            '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000', env=env)
    settings.write_text('[gd_resource type="EditorSettings" format=3]\n[resource]\n'
                        f'export/android/android_sdk_path = {json.dumps(str(sdk))}\n'
                        f'export/android/java_sdk_path = {json.dumps(str(java))}\n'
                        f'export/android/debug_keystore = {json.dumps(str(keystore))}\n'
                        'export/android/shutdown_adb_on_exit = false\n')
    preset = stage / 'export_presets.cfg'
    with preset.open('a') as file:
        file.write('\n[preset.1]\nname="Android development"\nplatform="Android"\nrunnable=true\n'
                   'advanced_options=false\nexport_filter="all_resources"\ninclude_filter="*.gpk,*.json,*.txt"\n'
                   'exclude_filter="bin/libgrandleon_godot.so,bin/grandleon_godot.dll,*.exe"\n'
                   '[preset.1.options]\n'
                   f'custom_template/debug={json.dumps(str(template_dir / "android_debug.apk"))}\n'
                   f'custom_template/release={json.dumps(str(template_dir / "android_release.apk"))}\n'
                   'gradle_build/use_gradle_build=false\n'
                   'architectures/armeabi-v7a=false\narchitectures/arm64-v8a=true\n'
                   'architectures/x86=false\narchitectures/x86_64=true\nversion/code=1\nversion/name="0.6-dev"\n'
                   f'package/unique_name={json.dumps(package_id)}\npackage/name={json.dumps("Grandleon UI checks" if args.ui_test else "Grandleon")}\n'
                   'launcher_icons/main_192x192="res://icon.png"\n'
                   'launcher_icons/adaptive_foreground_432x432="res://android_icon.svg"\n'
                   'launcher_icons/adaptive_background_432x432="res://android_background.svg"\n'
                   'splash_screen/icon="res://android_icon.svg"\n'
                   'splash_screen/background_color=Color(0.027451, 0.066667, 0.105882, 1)\n'
                   'splash_screen/disable_godot_boot_splash=true\n'
                   'permissions/internet=true\npermissions/change_wifi_multicast_state=true\npermissions/access_wifi_state=true\npermissions/read_external_storage=false\npermissions/write_external_storage=false\n'
                   'screen/support_small=true\nscreen/support_normal=true\nscreen/support_large=true\nscreen/support_xlarge=true\n'
                   'texture_format/s3tc_bptc=false\ntexture_format/etc2_astc=true\n')
    run_godot(godot, '--headless', '--editor', '--import', '--quit', '--path', stage, env=env)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=output.name + '.tmp-', dir=output.parent))
    try:
        apk = temporary / 'grandleon-android-development.apk'
        run_godot(godot, '--headless', '--path', stage, '--export-debug', 'Android development', apk, env=env)
        run(signer, 'verify', '--verbose', apk, env=env)
        run(sdk / 'build-tools' / BUILD_TOOLS / 'zipalign', '-c', '-P', '16', '4', apk, env=env)
        manifest = subprocess.check_output([
            str(sdk / 'build-tools' / BUILD_TOOLS / 'aapt'), 'dump', 'badging', str(apk),
        ], text=True, env=env)
        for expected in [f"name='{package_id}'", "sdkVersion:'24'", "targetSdkVersion:'36'"]:
            if expected not in manifest:
                raise ValueError(f'Unexpected Android manifest; missing {expected}')
        with zipfile.ZipFile(apk) as package:
            for abi in ABIS:
                library = package.read(f'lib/{abi}/libgrandleon_godot.so')
                if hashlib.sha256(library).hexdigest() != native_proof[abi]['sha256']:
                    raise ValueError(f'Exported APK native library mismatch: {abi}')
        revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
        dirty = subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True)
        (temporary / 'BUILD-INFO.json').write_text(json.dumps({
            'development': True, 'uiTest': args.ui_test, 'packageId': package_id, 'milestone': '0.6', 'sourceRevision': revision,
            'sourceTree': 'dirty' if dirty else 'clean', 'godotVersion': GODOT_VERSION,
            'templatesSha256': TEMPLATES_SHA256, 'ndkVersion': NDK_VERSION,
            'buildTools': BUILD_TOOLS, 'nativeLibraries': native_proof,
            'minimumSdk': 24, 'targetSdk': 36, 'apkSizeBytes': apk.stat().st_size,
            'apkSha256': digest(apk), 'applicationId': 'org.grandleon.app.dev',
            'signing': 'local development key, not an official release',
        }, indent=2) + '\n')
        temporary.rename(output)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    print(output)


if __name__ == '__main__':
    main()
