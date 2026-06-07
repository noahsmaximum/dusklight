<div align="center">
  <img src="res/logo.png" alt="Logo" width="640">

  <p align="center">
    <a href="https://twilitrealm.dev">Official Website</a>
    •
    <a href="https://discord.gg/6NpMhefCK9">Discord</a>
  </p>
</div>

# 🎲 Archipelago multiworld support (unofficial fork)

This is an **unofficial fork** of [TwilitRealm/dusklight](https://github.com/TwilitRealm/dusklight) that adds native
[Archipelago](https://archipelago.gg) multiworld randomizer support to the Twilight Princess PC port. All credit for
Dusklight itself belongs to the TwilitRealm team — see the original README below. The Archipelago code lives on the
**[`dusklight-Archipelago`](https://github.com/noahsmaximum/dusklight/tree/dusklight-Archipelago)** branch.

### What this adds

A native `dusk::archipelago` module (`src/dusk/archipelago.cpp`) that is the in-game side of the randomizer,
replacing the GameCube REL + `dolphin_memory_engine` approach used for the emulated (Dolphin) version:

- Hosts a localhost TCP server on **`127.0.0.1:17354`** for the Archipelago client.
- Exposes a read/write window over the live save (`dSv_info_c`) so the client can read location flags, current stage, health, etc.
- Grants received items in-game via the decomp's own `execItemGet()`, gated so it never fires mid-cutscene.
- Adds an **Archipelago** status panel under the in-game **Tools** menu.

### The two halves

| Repo | Role |
|------|------|
| **this repo** (native fork) | the game + the in-game AP bridge |
| [noahsmaximum/Dusklight-Archipelago](https://github.com/noahsmaximum/Dusklight-Archipelago) | the Archipelago client / world (seed generation, server, item & location logic) |

### Using it

1. **Build** this branch from source (see [Building](#building) below) — the AP module is compiled in automatically.
2. **Run** Dusklight with your disc and **load your save**:
   ```
   dusklight --dvd "path/to/Twilight Princess (USA).rvz"
   ```
   The bridge starts listening on `127.0.0.1:17354`; check **Tools → Archipelago** for status.
3. Generate a seed, host a server, and run the client from the
   [Archipelago world repo](https://github.com/noahsmaximum/Dusklight-Archipelago) (full instructions there), then connect.

> **Note:** your Archipelago slot name must match your in-game Twilight Princess **save file name**.
>
> ⚠️ Tested on the **GameCube USA (GZ2E01)** version on Windows.

---

*Original Dusklight README below.*

# Overview

Dusklight is a reverse-engineered reimplementation of Twilight Princess.

It aims to be as accurate as possible to the original while also providing new options, enhancements, and tools to customize your experience.

# Setup

> [!IMPORTANT]
> Dusklight does *not* provide any copyrighted assets. You must provide your own copy of the original game.

> [!IMPORTANT]
> At a minimum, Dusklight requires a GPU with support for either D3D12, Vulkan, or Metal. Your experience with specific hardware, operating systems, and drivers may vary. In particular, older Intel iGPUs have a high likelihood of incompatibility. We are also aware of a number of issues on devices with Adreno GPUs and are working to resolve them.

### 1. Dump your game

You must dump your own copy of the game, please see [this article](https://wiki.dolphin-emu.org/index.php?title=Ripping_Games) for instructions. After dumping, you can use a program like [Dolphin](https://dolphin-emu.org/) or [nodtool](https://github.com/encounter/nod/releases) to convert the `.iso` to a `.rvz` to save space.

Currently, only the GameCube USA and EUR releases are supported. Support for other versions of the game is planned in the future.

### 2. Download [Dusklight](https://github.com/TwilitRealm/dusklight/releases)

### 3. Setup the game
**Windows / macOS / Linux**
- Extract the .zip file
- Launch Dusklight
- Press **Select Disc Image** and provide the path to your supported game dump
- Press **Play**!

**iOS**
- Follow the [iOS setup guide](docs/ios-install-altstore.md)

**Android**
- Install the Dusklight APK
- Launch Dusklight
- Press **Select Disc Image** and provide the path to your supported game dump
- Press **Play**!

# Building

If you'd like to build Dusklight from source, please read the [build instructions](docs/building.md).

Pull requests are welcomed! Note that we do not accept contributions that are primarily AI-generated and will close your PR if we suspect as much. Please also see the [code conventions](docs/code-conventions.md).

# Credits

Special thanks to the [TP decompilation](https://github.com/zeldaret/tp) team, the GC/Wii decompilation community, the [Aurora](https://github.com/encounter/aurora) developers, the [TP speedrunning community](https://zsrtp.link), and all [contributors](https://github.com/TwilitRealm/dusklight/graphs/contributors).

<br/>
<div align="center">
    <a href="https://github.com/encounter/aurora">
        <img src="assets/aurora-powered.png" alt="Powered by Aurora" width="800">
    </a>
</div>
