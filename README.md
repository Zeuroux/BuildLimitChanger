# BuildLimitChanger

A mod to change the build limits of dimensions in Minecraft Bedrock Edition.

---
- Use v0.0.5 for versions below 1.21.120 and v0.0.6 for above

## How To Use

### 🖥️ Windows

#### 1. Download the Latest DLL File

- Go to the [**Releases page**](https://github.com/Zeuroux/BuildLimitChanger/releases) to download the latest `.dll` file for Windows.

#### 2. Inject the DLL

- You can use **any DLL injector**, but i **recomend**  [**FateInjector**](https://github.com/fligger/FateInjector)
- Steps:
  1. Launch **Minecraft Bedrock Edition**.  
  2. Open **FateInjector** (or your preferred injector).  
  3. Select the the downloaded dll
  4. Press inject

#### 3. Configuration and Log file location:
   ```
   %LOCALAPPDATA%/Packages/Microsoft.MinecraftUWP_8wekyb3d8bbwe/RoamingState/BuildLimitChanger/
   ```
---

### 📱 Android

#### 1. Download the Latest SO File

- Go to the [Releases page](https://github.com/Zeuroux/BuildLimitChanger/releases) to download the latest `.so` file.

#### 2. Installation

- **If you are using [LeviLauncher](https://github.com/LiteLDev/LeviLaunchroid):**
  1. Download the `.so` file.
  2. Tap the file on your file manager and choose to open with LeviLauncher for import.
  3. Launch

#### 3. Configuration and Log file location:
   ```
   /storage/emulated/0/games/BuildLimitChanger/
   ```
   or
   ```
   /storage/emulated/0/Android/data/[minecraft package name]/BuildLimitChanger/
   ```
---

### 🗄️ Server (Windows and Linux)

#### 1. Download the Latest Dll (Windows) or SO (Linux) File

- Go to the [Releases page](https://github.com/Zeuroux/BuildLimitChanger/releases) to download the latest one.

#### 2. Inject the DLL or SO

- You can use **any DLL injector**, fate injector will work for windows, you can google so injector cuz idk one

#### 3. Configuration and Log file location:

- same location as the executable file of the server (you'll find BuildLimitChanger folder)

## ⚠️ Important Warning

- Changing **Min Build Limit** will **drastically alter world generation**.  
- Lowering **Max Build Limit** below the game’s default can also significantly change terrain and structure generation.  
- Only modify these values in new worlds or after backing up existing saves.

> **Notes**
> - The default settings match the game’s default height ranges.

---

## 🐞 How to Report Crashes / Issues

When opening a **GitHub Issue**, **you must include** the following:

1. **Launcher/Injector used** (e.g., LeviLauncher, other injector name/version)  
2. **Minecraft version** (e.g., 1.21.100, 1.20.101, etc.)  
3. **Minecraft architecture** (e.g., `arm64-v8a`, `x86`, `x86_64`)  

**Also attach:**
- **BuildLimitChanger config file** you used  
- **`log.txt` file** (located in the same folder as the config)  

---
