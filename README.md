# PRISM

A neon twin-stick arena shooter in C++17 on SDL3. It has a spring-mass warping grid, bloom,
synthesized sound effects and music, and gamepad rumble.

## Build & run

```sh
make && ./prism            # --fullscreen, --no-rumble
```

If `libsdl3-dev` isn't installed, the build uses the SDL3 headers in `third_party/` and links
them against the SDL3 runtime already on the system (`libSDL3.so.0`).

## Controls

| Action | Gamepad | Keyboard / mouse |
|---|---|---|
| Move | Left stick | WASD |
| Aim & fire | Right stick | Hold LMB (aim with mouse) or arrow keys |
| Bomb | LB / RB / LT / RT | Space / RMB |
| Pause | Start | Esc |
| Confirm | A | Enter |
| Back / quit to title | Back (or B) | Esc / Q (when paused) |
| Music on/off | Y | M |
| Fullscreen | | F11 / Alt+Enter |

## Enemies

| | |
|---|---|
| Drifter (amber triangle) | Wanders around, drifting slowly toward you |
| Seeker (magenta kite) | Homes in on you |
| Weaver (green hexagon) | Sidesteps your bullets |
| Splitter (orange star) | Breaks into three spiralling Shards |
| Lancer (red needle) | Takes aim (watch the dotted line), then dashes across the arena. Takes 2 hits |
| Singularity (violet rings) | A gravity well. Pulls in you, your shots, and other enemies. Takes 16 hits. If it absorbs 10 enemies it bursts into Seekers |

Every kill builds your streak. The multiplier rises to X10 and resets when you die. At X3 your
gun becomes a 3-way spread; at X6 it gets 4 streams and a faster fire rate. You get extra lives
at 100k, 200k, 400k… and extra bombs at 75k, then every 150k.

## Testing

`--autoplay` lets a bot play. `--shot out.bmp --shot-at 30` saves a frame at 30 s and exits.
Test modes never open the gamepad, so they never rumble it.
