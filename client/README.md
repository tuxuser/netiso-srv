# NetISO client (Xbox 360 dashlaunch plugin)

Source of `netiso.xex`, the console side of NetISO. Imported verbatim from
[XeFreedom/NetISO](https://github.com/XeFreedom/NetISO) at commit `e4cbf38`
(2026-06-22), without the original Windows server (the Rust server in this
repo's root replaces it) and without `netIso.sln`, which references a
`testApp` project that is not in the upstream tree.

Licensed under the GPL-3.0, see [`LICENSE`](LICENSE).

| Path | What |
|---|---|
| `isodll.sln` | Solution with the two projects below |
| `netiso/` | The plugin: DVD IRP hooks, network client, ISO I/O worker thread |
| `netiso/xex.xml` | `imagexex` settings (system DLL, base address `0x91100000`) |
| `xkelib/` | Kernel and XAM export headers plus `.def` files, one configuration per XDK version |
| `xkelib/_tools/` | Scripts and `deffix.exe` to regenerate the `.def` files for another XDK version; not needed to build |
| `readme.txt` | Upstream install notes |

## Build

Requires Windows, Visual Studio 2010 and the Microsoft Xbox 360 SDK (XDK),
which adds the `Xbox 360` platform: the PowerPC compiler and linker,
`xboxkrnl.lib`, `xapilib.lib`, `xnet.lib`, `libcmt.lib` and `imagexex`. The XDK
is Microsoft-proprietary and not redistributable, so there is no public CI
build of this plugin.

1. Open `isodll.sln`.
2. Project `xkelib`: choose the configuration matching the installed XDK
   version (`Release20871.0` ... `Release21256.0`). Its pre-build step runs
   `lib /def:kernel\kernelext<ver>.def /MACHINE:PPCBE` (and the XAM
   counterpart) to produce `kernelext.lib` and `xamext.lib`.
3. Project `netiso`: configuration `Release`, platform `Xbox 360`. It links
   `xapilib.lib xboxkrnl.lib libcmt.lib xnet.lib` and xkelib with
   `/dll /entry:_DllMainCRTStartup /include:XboxKrnlBuildNumber /ALIGN:128,4096`,
   then `imagexex` wraps the DLL into `netiso.xex` using `xex.xml`.
4. The post-build step runs `xextool -m r -e d -r a` to turn the devkit image
   into one a retail RGH/JTAG console loads through dashlaunch. `xextool`
   (xorloser's XexTool, not part of the XDK) must be on `PATH`. The result is
   `netiso.xex` in the solution directory.

## Install

See [`readme.txt`](readme.txt). In short: copy `netiso.xex` to the console,
put the server's IP in `netiso.xex.txt` next to it, load it as a dashlaunch
plugin, and mount ISOs from the Aurora file manager (`Smb:` item). The wire
protocol is unchanged, so the plugin works with both the stock and the
low-latency server in this repo.
