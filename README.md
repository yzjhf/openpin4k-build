# openpin4k-build

Open-source build tooling that compiles the **Visual Pinball (VPX) Standalone** engine
([`vpinball/vpinball`](https://github.com/vpinball/vpinball), GPL) for the **AtGames Legends
Pinball 4K** (Rockchip **RK3588 / aarch64**) and packages it as an AtGames *External
Application* bundle.

It is the build half of the **OpenPin4K** project (the main project repo is private).

## Why this repo is public

Two practical reasons — **nothing private lives here**:

1. **Free ARM compiler.** The engine must be built on an ARM machine. GitHub's hosted ARM
   runners are **free for public repositories** and billed for private ones. Keeping just the
   build glue public makes every build free.
2. **GPL compliance.** VPX is GPL-licensed, so publishing the scripts used to build it is an
   obligation we have to meet anyway. This repo satisfies it.

This repo contains **only**: a GitHub Actions workflow, a ~60-line C launch wrapper, and a
menu-metadata file. **No pinball tables, no ROMs, no personal data, no proprietary or
DRM-protected files, and none of the OpenPin4K application code.** The compiled output is the
open-source VPX engine — itself already public source.

## What it produces

The **build-vpx** workflow runs on `ubuntu-22.04-arm` and:

1. Checks out a pinned `vpinball/vpinball` tag.
2. Builds its external dependencies (SDL3 etc. — cached between runs).
3. Compiles `VPinballX_BGFX` with `-DBUILD_RK3588=ON` (the cabinet's chip).
4. Packages everything into `external/vpx/` with a launch wrapper, an example table, and the
   menu icon/metadata.

Download the **`openpin4k-vpx-external-app`** artifact, unzip it, and copy the `external/`
folder to the root of a FAT32 USB stick → launch **vpx** from the cabinet's *External
Applications* menu. If it misbehaves, send back `external/vpx/vpx-log.txt`.

## License

Build scripts and the launch wrapper: see [LICENSE](LICENSE) (GPL-3.0, matching upstream VPX).
The compiled engine is © the Visual Pinball team under their license.
