# Suyu Build Scripts

This directory contains utility scripts to help with building and maintaining the Suyu project.

## Vcpkg Build Fix Scripts

### `fix-vcpkg-build.ps1` (Recommended)
Comprehensive automated script that resolves vcpkg build issues:
- Cleans existing boost installations
- Clears vcpkg cache and buildtrees
- Reinstalls dependencies in correct order
- Provides troubleshooting guidance

**Usage:**
```powershell
.\scripts\fix-vcpkg-build.ps1
```

### `clean-boost.ps1`
Specialized script for cleaning boost components only:
- Removes all boost packages using boost-uninstall
- Useful for boost-specific issues

**Usage:**
```powershell
.\scripts\clean-boost.ps1
```

### `clean-boost.bat`
Batch file version of the boost cleanup script for environments without PowerShell.

**Usage:**
```cmd
scripts\clean-boost.bat
```

## Common Issues Resolved

### Missing vcpkg-cmake Configuration Files
**Error:** `include could not find requested file: vcpkg-cmake/vcpkg-port-config.cmake`
**Solution:** Run `fix-vcpkg-build.ps1` to install dependencies in correct order

### Boost Version Conflicts
**Error:** `no version database entry for boost-cobalt at 1.80.0`
**Solution:** Run `clean-boost.ps1` then reinstall with `vcpkg install`

### Build Cache Issues
**Error:** Various build failures after vcpkg updates
**Solution:** Use `fix-vcpkg-build.ps1` to clean cache and reinstall
