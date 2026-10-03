# vibrance-icc

Generates ICC profiles that apply the same saturation boost `vibrant-cli` did
to the whole screen, for Wayland compositors that support `color_profile icc`
(Sway ≥ 1.10).

The formula is libvibrant's, literally:

```
output = s * color + (1 - s) * average(R, G, B)
```

- `s = 1.0` → no change
- `s > 1.0` → more saturated (the useful range is between 1.2 and 1.6)
- `s < 1.0` → less saturated; `s = 0` leaves the image in grayscale

## Why an ICC profile

`vibrant-cli` loaded that matrix straight into the GPU as a CTM through X11
(`XRRSetCrtcTransform` and the `CTM` property), something that does not exist
on Wayland. What Sway does offer is loading an ICC profile per output, so this
program **bakes** the transformation into a profile: it describes a "monitor"
whose conversion from sRGB is exactly the formula above, and the compositor
applies it to everything it paints.

Because the formula mixes the three channels into each other, it cannot be
expressed with a matrix and three gamma curves: the profile is of the table
kind (`lut16Type`, ICC v2, with the PCS in Lab), with a grid of `grid³` points
per direction.

## Requirements

- Sway ≥ 1.10 with the Vulkan renderer (`WLR_RENDERER=vulkan`)
- A C11 compiler and libm. Nothing else: no libvibrant, no lcms, no X11.

## Building and installing

```sh
make
sudo make install          # installs into /usr/local/bin
```

To install elsewhere: `make install PREFIX=~/.local`.
To uninstall: `sudo make uninstall`.

## Usage

```sh
vibrance-icc 1.4                      # creates vibrance-1.40.icc
vibrance-icc 1.4 -o ~/.config/vibrance.icc
vibrance-icc 0.0 -o gray.icc          # grayscale
vibrance-icc 1.4 --grid 33            # smaller and faster profile
```

| Option | What it does |
| --- | --- |
| `-o, --output FILE` | destination (default `vibrance-<saturation>.icc`) |
| `-g, --grid N` | table points per axis, 17 to 65 (default 65) |
| `-f, --force` | overwrite the file if it already exists |
| `-q, --quiet` | print nothing if all goes well |
| `--selftest` | check the profile in memory, without writing anything |
| `-V, --version`, `-h, --help` | |

The supported saturation range is 0.05 to 4.0.

## Integrating into Sway

Generate the profile and point to it from `~/.config/sway/config`:

```
output * color_profile icc /home/your-user/.config/vibrance.icc
```

Use an absolute path. If you only want to affect one monitor, put its name
(the one `swaymsg -t get_outputs` shows) instead of `*`:

```
output DP-1 color_profile icc /home/your-user/.config/vibrance.icc
```

The Vulkan renderer is required. If you do not already have it by default,
start Sway with `WLR_RENDERER=vulkan sway`.

Reload with `swaymsg reload`, or try a value live without touching the
configuration:

```sh
swaymsg output '*' color_profile icc /path/to/profile.icc
swaymsg output '*' color_profile srgb      # back to normal
```

## Choosing the value

Generate two or three profiles and switch between them live with `swaymsg`,
looking at a photo with skin tones: they are the first thing that looks wrong
when the value is too high. 1.3–1.4 is usually the point where green and blue
gain without faces turning orange.

Keep in mind that the formula clips: with a high `s`, colors that are already
near the edge of the sRGB space saturate all the way to the limit and lose
detail. That is identical to what `vibrant-cli` did, not a flaw in this
program.

## Performance

The cost grows with the cube of `--grid`, and the file along with it. Measured
on a desktop machine:

| grid | points per table | size | time |
| --- | --- | --- | --- |
| 17 | 4,913 | 58 KiB | 7 ms |
| 33 | 35,937 | 422 KiB | 26 ms |
| 49 | 117,649 | 1.3 MiB | 60 ms |
| 65 | 274,625 | 3.1 MiB | 134 ms |

The default (65) is the highest the format sensibly supports and the one that
leaves the least interpolation error. It is generated only once, so there is
no reason to lower it unless the file size bothers you.

The profile shares a single table between the three `A2B` tags and another
between the three `B2A` ones, so it holds two tables instead of six.

## Verifying the profile

```sh
vibrance-icc --selftest 1.4
```

It walks a grid of colors, converts them to Lab with the `A2B` table and
brings them back with the `B2A` one, and reports the worst round-trip error.
It should stay below 20 out of 65535 (0.03%), which is the quantization noise
of ICC v2's 16-bit Lab encoding, not an error in the computation. The colors
the formula clips are not invertible and are counted separately.

If you have lcms2 installed, you can check the real effect: a shifted mid gray
should come out more saturated after going through the profile.

```sh
vibrance-icc 1.4 -o /tmp/v.icc
printf '150\n120\n100\n' | transicc -i '*sRGB' -o /tmp/v.icc -n -w
# -> 160.6  118.7  90.6     (from 150 120 100)
```

With `s = 1.0` the same command should return the input values unchanged: that
is how you check the ICC pipeline does not introduce any bias of its own.

## License

GPL-3.0-or-later. The full text is in [LICENSE](LICENSE).

The ICC profiles the program generates are not derivative works of its code:
their copyright tag says `Public domain` and you can distribute them however
you like.

## Credits

The saturation formula comes from
[libvibrant](https://github.com/libvibrant/libvibrant) and `vibrant-cli`.
This program shares no code with them: it only reproduces the same effect by
the route Wayland supports.
