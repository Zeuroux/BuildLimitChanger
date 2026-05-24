# BuildLimitChanger

A mod that lets you change the build height and depth limits in Minecraft Bedrock Edition.

[Windows](#windows-minecraft-bedrock) · [Android](#android) · [Server (Windows)](#minecraft-bedrock-server-windows) · [Server (Linux)](#minecraft-bedrock-server-linux)
> [!NOTE]
> Launch the game or server with the mod at least once to generate the configuration file.

> [!WARNING]
> You must edit the config.ini file and rejoin the world for any changes to take effect.

> [!IMPORTANT]
> Read all comments in the config file before opening an issue or asking for help.

---

# Windows (Minecraft Bedrock)

### 1. Download the Mod

* Open the [BuildLimitChanger Releases page](https://github.com/Zeuroux/BuildLimitChanger/releases)
* Download the latest `.dll` file for Windows.

### 2. Inject the DLL

You need a DLL injector to load the mod into Minecraft.

Recommended injector:

* [FateInjector](https://github.com/fligger/FateInjector)

### 3. Load the Mod

1. Start Minecraft Bedrock Edition.
2. Open FateInjector.
3. Select the downloaded `.dll` file.
4. Click **Inject**.

### Configuration Folder

```
%LOCALAPPDATA%/Packages/Microsoft.MinecraftUWP_8wekyb3d8bbwe/RoamingState/BuildLimitChanger/
```
Or for preview:
```
%LOCALAPPDATA%/Packages/Microsoft.MinecraftWindowsBeta_8wekyb3d8bbwe/RoamingState/BuildLimitChanger/
```
---

# Android

### 1. Download the Mod

* Open the [BuildLimitChanger Releases page](https://github.com/Zeuroux/BuildLimitChanger/releases)
* Download the latest `.so` file for Android.

### 2. Install with LeviLauncher

If you are using [LeviLauncher](https://github.com/LiteLDev/LeviLaunchroid) or [Ambient](https://play.google.com/store/apps/details?id=io.kitsuri.mayape):

1. Download the `.so` file.
2. Open your file manager.
3. Tap the `.so` file.
4. Choose **Open with (Launcher)**.
5. Launch.
6. Modify the generated config once in the launcher screen.

### Configuration Folder

```
/storage/emulated/0/games/BuildLimitChanger/
```
Or
```
/storage/emulated/0/Android/media/(launcher)/BuildLimitChanger/
```

---

# Minecraft Bedrock Server (Windows)

### 1. Download the Mod

* Open the [BuildLimitChanger Releases page](https://github.com/Zeuroux/BuildLimitChanger/releases)
* Download the latest Windows `.dll` file.

### 2. Inject the DLL

Use any DLL injector to load the mod into the server.

Recommended:

* [FateInjector](https://github.com/fligger/FateInjector)

### Configuration Folder

The `BuildLimitChanger` folder will be created in the same folder as the server executable.

---

# Minecraft Bedrock Server (Linux)

### 1. Download the Mod

* Open the [BuildLimitChanger Releases page](https://github.com/Zeuroux/BuildLimitChanger/releases)
* Download the latest Linux `.so` file.

### 2. Move the File

Place the `.so` file in the same folder as `bedrock_server`.

### 3. Start the Server with the Mod

```bash
LD_PRELOAD=./linux-x86_64-libBuildLimitChanger.so ./bedrock_server
```

### Optional: Make It Permanent

You can use `patchelf` so you do not need to use the command above every time you start the server.

### Configuration Folder

The `BuildLimitChanger` folder will appear in the same folder as the server executable.
