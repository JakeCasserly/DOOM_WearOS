# DOOM_WearOS

This is a port of the classic DOOM (1993) for wearable android device.

**Currently only tested with shareware doom and freedoom phase 1**

If you want to try a different version (such as shareware version of DOOM you can find for free: *doom1.wad*) please replace with your own WAD file (game data) in the /assets folder.

<img width="323" height="325" alt="Screenshot 2026-01-13 200229" src="https://github.com/user-attachments/assets/17c06f05-84ed-4a30-9194-afb70bc80ab8" />
<img width="307" height="340" alt="Screenshot 2026-01-13 200244" src="https://github.com/user-attachments/assets/09dc63cd-6873-4575-bc46-282373e5eca7" />

## Development and build environment:

Android Studio

## Build instructions:

The simulations have been compiled and run within android studio. They are also done using the SDK with the latest API version 36 (as of current).

The device shown in the photos is Wear OS Large Round API 36 "Baklava" and is simulated with 750 MB RAM and 4 CPU cores.

### Music

In order to properly play sound tracks you must provide the appropriate .ogg files and place them within the /assets folder (same directory as the WAD file). This project does not make use of the MUS files within the provided WAD file.

OGG files used in this project were found at https://sc55.duke4.net/games.php under the "Doom/Ultimate Doom" section.

## TODO:

- Fix screen resolution. (Complete)
- Fixing joystick and buttons. (Joystick removed for more traditional tank controls, buttons work fine but slightly temperamental)
- Build and run performance test on real device (Tested on Galaxy Watch 7, performs very well)
- Implement music (Complete)

## References

#### All credit for core android port goes to this repository here:
https://github.com/deqart/Doom-Android

#### Doomgeneric (portable version of doom all of this is built upon):
https://github.com/ozkl/doomgeneric?tab=readme-ov-file
