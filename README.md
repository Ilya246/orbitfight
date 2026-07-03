# orbitfight
A game where you're a spaceship shooting other spaceships in a very large solar system simulated as a N-body gravity simulation, other mechanics might possibly be added later

Has a matrix server: https://matrix.to/#/#orbitfight:matrix.org

This game is more of a learning project (and is also my (Ilya246) first "real" C++ program) than a real game and is not really intended to be particularly interesting, another side effect of which is that the code may be rather cursed in places

Development currently halted, full recode planned an unknown amount of time into the future

On screenshot: missile fight between ships orbiting different planets

![image](https://github.com/Ilya246/orbitfight/assets/57039557/8e7c2101-0b3e-4d8f-8240-065d2559d608)

# Controls
- W - forwards
- A - rotate left
- S - backwards
- D - rotate right
- X - fire railgun
- LCtrl - boost
- Spacebar - fire
- T - target body closest to cursor
- Tab - set/change/unset reference body to predict trajectories against
- M - place a maneuver node on the trajectory near the cursor (requires trajectory prediction active)
- Shift+N - clear all maneuver nodes

# Maneuver Scheduler
When trajectory prediction is active (press Tab near a body), press M to place a KSP-style maneuver node on your predicted trajectory. Each node has a button panel for setting the burn orientation (prograde, retrograde, normal, antinormal, radial in/out, or manual toward mouse). Scroll the wheel over a node to adjust its delta-V. Toggle Auto to have your ship automatically execute the burn when it reaches the node — this setting carries over as the default for new nodes. Two ghost trajectories are shown per maneuver chain: a cyan ghost showing the ideal post-burn path (instant delta-V), and a green ghost demonstrating the physical burn (turn + thrust).

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

# Font
Using Hack font, not owned by us
