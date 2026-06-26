# Commit and Release Process

Starcluster is configured to automatically build cross-platform native launchers every time you push to the `main` branch on GitHub.

## 1. Commit and Push
When you are ready to release an update or backup your code:

```bash
git add .
git commit -m "Your commit message here"
git push origin main
```

## 2. Automated Build
Pushing to the `main` branch triggers the GitHub Actions workflow (`.github/workflows/build.yml`). This workflow automatically spins up virtual environments and compiles the game for:
- **macOS**: Installs SDL2 via Homebrew, builds the binary, packages it into a standard `Starcluster.app` bundle, and generates a `.dmg` disk image.
- **Windows**: Uses MSYS2 and MinGW-w64 to compile a Windows executable, bundles it with `SDL2.dll`, and compresses it into a `.zip` archive.
- **Linux**: Installs `libsdl2-dev` via APT, compiles the binary, and publishes it as an executable file.

## 3. Downloading Launchers
To access the compiled launchers:
1. Go to your repository on GitHub.
2. Click the **"Actions"** tab.
3. Click on the latest workflow run (e.g., "Build Launchers").
4. Scroll down to the **Artifacts** section at the bottom of the summary page.
5. You will see three downloadable packages:
   - `Starcluster-macOS` (contains the `.dmg` file)
   - `Starcluster-Windows` (contains the `starcluster.exe` and `.dll`)
   - `Starcluster-Linux` (contains the executable binary)

*Note: GitHub automatically zips artifacts, so the DMG and Linux binaries will be inside a downloaded zip file.*
