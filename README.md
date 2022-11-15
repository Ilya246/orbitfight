# orbitfight
A very in-development game where you're a spaceship shooting other spaceships in a very large solar system simulated as a N-body gravity simulation, other mechanics might possibly be added later

Development progress is posted in the matrix server: https://matrix.to/#/#orbitfight:matrix.org

This game is more of a learning project (and is also my (Ilya246) first "real" C++ program) than a real game and is not really intended to be particularly interesting, another side effect of which is that the code may be rather cursed in places

On screenshot: myself orbiting a moon in a planetary system after having shot a projectile, trajectories selected to be drawn relative to the main planet

![preview](https://user-images.githubusercontent.com/57039557/175764692-c55b948b-7c8f-4055-b6cb-bed32db5f239.png)

# Controls
- W - forwards
- A - rotate left
- S - backwards
- D - rotate right
- LShift - charge up hyperboost (NOT a fuel bar)
- LCtrl - boost | afterburn during hyperboost
- Spacebar - fire
- T - target body closest to cursor
- Tab - set/change/unset reference body to predict trajectories against

# Hosting
`orbitfight.exe --headless`

# Config
Config documentation should generate upon starting the executable if a config file is not detected. If you want to regenerate it, `orbitfight.exe --regenerate-help`

# Compiling
To compile orbitfight, you have to:
- make sure `src/*.cpp` is accounted for
- make sure `include/*.hpp` is accounted for
- make sure SFML `graphics`, `window`, `network` and `system` modules are accounted for
- make sure you're using at least C++20
- if you plan on running the game in client mode, put the assets folder next to the game executable after compiling

# Font
Using Hack font. Download it manually and place at `assets/font.ttf`.
