#!/usr/bin/env python
"""Build the NetHack GDExtension, its data files and the headless test.

    scons platform=<linux|macos|windows> target=<template_debug|template_release>
          [arch=universal] [tests=yes]

Outputs:
    godot/bin/libnethack.<platform>.<target>[.<arch>].<so|dll>
        (macOS: godot/bin/libnethack.macos.<target>.framework/...)
    godot/nethack_data/*      NetHack data files (copied to user://nethack/
                              by the game at start-up)
    build/<platform>/nethack_native_test[.exe]   (tests=yes)
"""

import glob
import os
import shutil
import subprocess
import sys

NH = "thirdparty/NetHack"
LUA = NH + "/submodules/lua"
GODOT_CPP = "thirdparty/godot-cpp"

env = SConscript(GODOT_CPP + "/SConstruct", {"api_version": "4.7"})

opts = Variables([], ARGUMENTS)
opts.Add(BoolVariable("tests", "Build the headless native test program", False))
opts.Update(env)

platform = env["platform"]
is_windows = platform == "windows"
is_msvc = env.get("is_msvc", False)

# -- NetHack core -------------------------------------------------------------

nh_env = env.Clone()
nh_env.Append(
    CPPPATH=[
        "native/include",  # zlib.h shim, nhlua.h
        "native/bridge",
        NH + "/include",
        LUA,
    ]
)
nh_env.Append(
    CPPDEFINES=[
        "SHIM_GRAPHICS",
        "NOTTYGRAPHICS",
        "NOCRASHREPORT",
        "ZLIB_COMP",
        "NOMAIL",
        "NOCWD_ASSUMPTIONS",
    ]
)
if is_windows:
    nh_env.Append(CPPPATH=[NH + "/sys/windows"])
    nh_env.Append(
        CPPDEFINES=[
            "WIN32",
            "_WIN32",
            "_CRT_NONSTDC_NO_DEPRECATE",
            "_CRT_SECURE_NO_DEPRECATE",
            "_SCL_SECURE_NO_DEPRECATE",
            "HAS_STDINT_H",
            # MSVC does not set __STDC_VERSION__ by default, so integer.h
            # would skip <inttypes.h> and sfbase.c's PRId64 & co. would be
            # undefined; upstream's own MSVC projects define this too.
            "HAS_INTTYPES_H",
            "HAS_INLINE",
        ]
    )
    if env["arch"] == "x86_64":
        nh_env.Append(CPPDEFINES=["WIN64", "_WIN64"])
    nh_env.Append(LIBS=["advapi32", "user32", "shell32"])
else:
    nh_env.Append(CPPDEFINES=["_GNU_SOURCE"] if platform == "linux" else [])

force = os.path.abspath("native/port/nhgodot_force.h")
core_env = nh_env.Clone()
if is_msvc:
    core_env.Append(CFLAGS=["/FI" + force])
else:
    core_env.Append(CFLAGS=["-include", force, "-w"])

core_src = sorted(glob.glob(NH + "/src/*.c"))
# cfgfiles.c gets NHGODOT_CFGFILES so nhgodot_force.h can make its sysconf
# check look in the game directory (see that header).
cfg_src = [f for f in core_src if os.path.basename(f) == "cfgfiles.c"]
core_src = [f for f in core_src if f not in cfg_src]
cfg_env = core_env.Clone()
cfg_env.Append(CPPDEFINES=["NHGODOT_CFGFILES"])
sys_src = [NH + "/sys/share/pmatchregex.c"]
if not is_windows:
    # sys/share/unixtty.c is replaced by native/port/nhgodot_unix.c: it needs
    # curses and drives a terminal the Godot process does not have.
    sys_src += [
        NH + "/sys/unix/unixunix.c",
        NH + "/sys/unix/unixres.c",
        NH + "/sys/share/ioctl.c",
    ]
port_src = sorted(glob.glob("native/port/*.c"))

lua_env = env.Clone()
lua_env.Append(CPPPATH=[LUA])
if not is_msvc:
    lua_env.Append(CFLAGS=["-w"])
if platform in ("linux", "macos"):
    lua_env.Append(CPPDEFINES=["LUA_USE_POSIX"])
lua_skip = {"lua.c", "luac.c", "onelua.c", "ltests.c"}
lua_src = [
    f for f in sorted(glob.glob(LUA + "/*.c")) if os.path.basename(f) not in lua_skip
]
if not lua_src:
    print("ERROR: %s is empty; run 'git submodule update --init --recursive'" % LUA)
    Exit(1)

obj_dir = "build/%s/%s/%s" % (platform, env["target"], env["arch"])


def objects(e, files, sub):
    out = []
    for f in files:
        name = os.path.splitext(os.path.basename(f))[0]
        out.append(e.SharedObject("%s/%s/%s" % (obj_dir, sub, name), f))
    return out


forced_objs = (
    objects(core_env, core_src, "core")
    + objects(cfg_env, cfg_src, "core")
    + objects(core_env, sys_src, "sys")
    + objects(core_env, port_src, "port")
)
# The scanner does not see a header pulled in with -include / /FI, so tell
# SCons that every file compiled with it must be rebuilt when it changes.
Depends(forced_objs, force)
core_objs = (
    forced_objs
    + objects(lua_env, lua_src, "lua")
    + objects(nh_env, glob.glob("native/bridge/*.cpp"), "bridge")
)

# -- GDExtension --------------------------------------------------------------

gd_env = nh_env.Clone()
gd_env.Append(CPPPATH=["native/src"])
gd_objs = objects(gd_env, sorted(glob.glob("native/src/*.cpp")), "gd")

suffix = env["suffix"]
if platform == "macos":
    lib_path = "godot/bin/libnethack.{0}.{1}.framework/libnethack.{0}.{1}".format(
        platform, env["target"]
    )
else:
    lib_path = "godot/bin/libnethack{}{}".format(suffix, env["SHLIBSUFFIX"])
library = gd_env.SharedLibrary(lib_path, source=core_objs + gd_objs)
env.NoCache(library)
Default(library)

# -- headless test ------------------------------------------------------------

if env["tests"]:
    test_env = nh_env.Clone()
    test_env.Append(CPPPATH=["native/bridge"])
    if not is_windows:
        test_env.Append(LIBS=["pthread"])
        if platform == "linux":
            test_env.Append(LIBS=["m"])
    # The test is an ordinary executable: drop shared-library-only link flags.
    for flag in ("-shared", "-dynamiclib", "/DLL"):
        if flag in test_env["LINKFLAGS"]:
            test_env["LINKFLAGS"].remove(flag)
    test_main = test_env.Object(
        "%s/test/native_test" % obj_dir, "native/test/native_test.cpp"
    )
    test_prog = test_env.Program(
        "build/%s/nethack_native_test" % platform, [test_main] + core_objs
    )
    Default(test_prog)

# -- makedefs (host tool) and data files ---------------------------------------

host_env = Environment(ENV=os.environ)
# native/include supplies nhlua.h, which hack.h and alloc.c include.
host_env.Append(CPPPATH=["native/include", NH + "/include", LUA])
if host_env["PLATFORM"] == "win32":
    host_env.Append(CPPPATH=[NH + "/sys/windows"])
    host_env.Append(
        CPPDEFINES=["WIN32", "_CRT_SECURE_NO_DEPRECATE", "_CRT_NONSTDC_NO_DEPRECATE"]
    )
host_env.Append(CPPDEFINES=["NOCWD_ASSUMPTIONS"])
makedefs_src = [NH + "/util/makedefs.c", NH + "/util/panic.c"] + [
    NH + "/src/" + f for f in ("monst.c", "objects.c", "date.c", "alloc.c", "hacklib.c")
]
makedefs = host_env.Program(
    "build/host/util/makedefs",
    [
        host_env.Object("build/host/obj/" + os.path.splitext(os.path.basename(f))[0], f)
        for f in makedefs_src
    ],
)

MAKEDEFS_OUT = ["data", "rumors", "oracles", "engrave", "epitaph", "bogusmon", "options"]
DAT_FILES = (
    sorted(os.path.basename(f) for f in glob.glob(NH + "/dat/*.lua"))
    + "help hh cmdhelp keyhelp history opthelp optmenu usagehlp wizhelp "
    "tribute symbols license".split()
)
if is_windows:
    DAT_FILES.append("porthelp")


def stage_data(target, source, env):
    md = os.path.abspath(str(source[0]))
    work = os.path.abspath("build/mkdat")
    shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(NH + "/dat", os.path.join(work, "dat"))
    os.makedirs(os.path.join(work, "include"))
    os.makedirs(os.path.join(work, "util"))
    for flag in ("-d", "-r", "-h", "-1", "-2", "-3", "-v"):
        subprocess.check_call([md, flag], cwd=os.path.join(work, "util"))
    out_dir = os.path.abspath("godot/nethack_data")
    os.makedirs(out_dir, exist_ok=True)
    for name in MAKEDEFS_OUT:
        shutil.copy2(os.path.join(work, "dat", name), out_dir)
    for name in DAT_FILES:
        src = (
            NH + "/sys/windows/porthelp" if name == "porthelp" else NH + "/dat/" + name
        )
        shutil.copy2(src, out_dir)
    shutil.copy2("native/data/sysconf", out_dir)
    # Godot only exports files it knows about; a manifest lets the game
    # copy every data file out of the PCK without directory listing.
    names = sorted(set(MAKEDEFS_OUT + DAT_FILES + ["sysconf"]))
    with open(os.path.join(out_dir, "manifest.txt"), "w", newline="\n") as fh:
        fh.write("\n".join(names) + "\n")
    return None


data_targets = ["godot/nethack_data/manifest.txt"]
data = env.Command(
    data_targets,
    [makedefs]
    + glob.glob(NH + "/dat/*")
    + ["native/data/sysconf"],
    Action(stage_data, "Staging NetHack data files into godot/nethack_data"),
)
Default(data)
