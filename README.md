# wdisplays

[![License: GPL 3.0 or later][license-img]][license-spdx]

wdisplays is a graphical application for configuring displays in Wayland
compositors. It borrows some code from [kanshi]. It should work in any
compositor that implements the wlr-output-management-unstable-v1 protocol.
Compositors that are known to support the protocol are [Sway], [Wayfire] and
[Hyprland].
The goal of this project is to allow precise adjustment of display settings in
any multi-monitor setup.

![Screenshot](wdisplays.png)

# Installation

[![Repology][repology-img]][repology-pkg]

Check your distro for a `wdisplays` package. Known distro packages:

- [Alpine](https://pkgs.alpinelinux.org/package/edge/testing/x86_64/wdisplays)
- [Arch](https://aur.archlinux.org/packages/wdisplays-git/)
- [Debian](https://packages.debian.org/sid/wdisplays)
- [Fedora](https://packages.fedoraproject.org/pkgs/wdisplays/wdisplays)
- [FreeBSD](https://svnweb.freebsd.org/ports/head/x11/wdisplays/)
- [Nix](https://github.com/NixOS/nixpkgs/tree/master/pkgs/tools/graphics/wdisplays)
- [OpenSUSE](https://build.opensuse.org/package/show/home%3AMWh3/wdisplays)

# Building

Build requirements are:

- meson
- GTK+3
- epoxy
- wayland-client

```sh
meson setup build
ninja -C build
sudo ninja -C build install
```

Saving to the kanshi config is built in by default; pass `-Dkanshi=disabled`
to `meson setup` to leave it out.

# Usage

Displays can be moved around the virtual screen space by clicking and dragging
them in the preview on the left panel. By default, they will snap to one
another. Hold Shift while dragging to disable snapping. You can click and drag
with the middle mouse button to pan. Zoom in and out either with the buttons on
the top left, or by holding Ctrl and scrolling the mouse wheel. Fine tune your
adjustments in the right panel, then click apply.

There are some options available by clicking the menu button on the top left:

- Automatically Apply Changes: Makes it so you don't have to hit apply. Disable
  this for making minor adjustments, but be careful, you may end up with an
  unusable setup.
- Show Screen Contents: Shows a live preview of the screens in the left panel.
  Turn off to reduce energy usage.
- Overlay Screen Names: Shows big names in the corner of all screens for easy
  identification. Disable if they get in the way.
- Save to kanshi Config: Writes the applied layout to your [kanshi] config.
  See the FAQ below.

These options are remembered between runs. To override one for a single run,
start wdisplays with `--auto-apply` or `--no-auto-apply`, `--preview` or
`--no-preview`, `--overlay` or `--no-overlay`, `--kanshi` or `--no-kanshi`.

# FAQ

### What is this?

It's intended to be the Wayland equivalent of an xrandr GUI, like [ARandR].

### I'm using Sway, why aren't my display settings saved when I log out?

Sway, like i3, doesn't save any settings unless you put them in the config
file. See man `sway-output`. If you want to have multiple configurations
depending on the monitors connected, you'll need to use an external program
like [kanshi] or [way-displays].

wdisplays can do this for you with kanshi: turn on "Save to kanshi Config" in
the menu. Apply then writes the layout into the kanshi profile for the
connected monitors, or adds a new one, and reloads kanshi. With automatic apply
on, the layout is saved when you turn it off, close wdisplays, or plug or
unplug a monitor. New profiles name outputs by make, model and serial.

The config is `$XDG_CONFIG_HOME/kanshi/config` (usually
`~/.config/kanshi/config`), or the file named by `WDISPLAYS_KANSHI_CONFIG`.
Start kanshi from your compositor config, for Sway:

```
exec kanshi
```

### I'm using Hyprland, why does my layout change back?

Hyprland keeps a layout applied by wdisplays only while wdisplays is open. The
next time it reloads its monitor rules, for example on a hotplug, it goes back
to the `monitor` rules in your Hyprland config. Put the layout there to keep
it.

### How do I add support to my compositor?

A minimal amount of code (approximately 150-200 LOC) is currently required to
get support for this in wlroots compositors. See the diff here for a sample
implementation on top of tinywl: [tinywl-output-management].

[kanshi]: https://github.com/emersion/kanshi
[way-displays]: https://github.com/alex-courtis/way-displays
[Sway]: https://swaywm.org
[Wayfire]: https://wayfire.org
[Hyprland]: https://hyprland.org
[ARandR]: https://christian.amsuess.com/tools/arandr/
[tinywl-output-management]: https://git.sr.ht/~jf/tinywl-output-management/commit/87a45d89ae0e7975e2a59f84e960380dd2f5ac08

[license-img]:  https://img.shields.io/badge/License-GPL%203.0%20or%20later-blue.svg?logo=gnu
[license-spdx]: https://spdx.org/licenses/GPL-3.0-or-later.html
[repology-img]: https://repology.org/badge/tiny-repos/wdisplays.svg
[repology-pkg]: https://repology.org/project/wdisplays/versions
