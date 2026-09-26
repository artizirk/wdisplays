# Changelog

The format is based on [Keep a Changelog][1].

This project tries to adhere to [Semantic Versioning][2].

## [Unreleased]

## [1.3.0] - 2026-09-26

First release of this fork, as upstream has seen no activity since 1.1.3.
1.2.0 is skipped to avoid a clash with JasonGantner's unreleased 1.2.0.

### Added

Save applied layouts to the kanshi config, optional (petertheprocess, JasonGantner, zipproth, Mars-Wave) [#11]
Remember the menu options between runs (mlaga97, Mars-Wave) [#33]
Command-line options to override the menu options for one run (Mars-Wave)
Apply pending changes with Ctrl+Return (0x17de) [#35]
Russian translation of the desktop file (wehrwolfmann) [#38]
Allow negative output positions (Mars-Wave) [#22]

### Changed

Capture screen previews a few times per second, not every frame (Mars-Wave) [#31]
Apply automatic changes when a drag ends, not on every step (Mars-Wave)
Send the position, scale and transform of every output on apply (Mars-Wave)
Skip automatic applies that change nothing (Mars-Wave)

### Fixed

Crashes around the screen name overlays (save196, Mars-Wave) [#34] [#17] [#18] [#30]
Crashes on close and when unplugging monitors (Mars-Wave) [#23]
Window not growing to fit its content (ngtiendungjb, Mars-Wave) [#37] [#5]
Snapping next to outputs with fractional scales (Mars-Wave) [#20]
Black corners on the screen name overlays (Mars-Wave) [#24]
Build warnings with newer meson and GLib (JasonGantner, Mars-Wave) [#32]
Disabled outputs turned back on by unrelated applies (Mars-Wave)
Canvas size of scaled, offset and rotated outputs (Mars-Wave)

## [1.1.3] - 2025-07-25

### Fixed

Fixed tiny GTK dropdown menu on Hyprland (Aleksanaa)

## [1.1.2] - 2025-07-25

### Fixed

Fixed compilation error with new GCC (ceamac)

## [1.1.1] - 2023-07-01

### Added

Added QtCreator files to .gitignore (redtide)
.editorconfig and .clang-format (redtide)
this file (redtide)

### Changed

Install icon to the app_id location (somini)
Move the output name overlay to the bottom left (WhyNotHugo)
Bump to current development version, use semver (redtide)
Updated desktop file (redtide)

### Fixed

Fixed license SPDX ID in main meson.build

## [1.1] - 2023-04-18

### Added

Added categories to the program shortcut (IntinteDAO)

### Changed

Add package links (Jason Francis)
Backport GTK4 changes (Jason Francis)
Create WdHeadForm class (Jason Francis)
Update README (Jason Francis)
Use correct versions when binding globals (Simon Ser)

## [v1.0] - 2020-05-09

First release after <https://github.com/MichaelAquilina/wdisplays> fork
(backup of the original, deleted repository at <https://github.com/cyclopsian/wdisplays>)

### Changed

Update application ID and readme (Jason Francis)


[1]: https://keepachangelog.com/en/1.0.0/
[2]: https://semver.org/spec/v2.0.0.html

[Unreleased]: https://github.com/Mars-Wave/wdisplays/compare/1.3.0...HEAD
[1.3.0]:  https://github.com/Mars-Wave/wdisplays/compare/1.1.3...1.3.0
[1.1.3]:  https://github.com/artizirk/wdisplays/compare/1.1.2...1.1.3
[1.1.2]:  https://github.com/artizirk/wdisplays/compare/1.1.1...1.1.2
[1.1.1]:  https://github.com/artizirk/wdisplays/compare/1.1...1.1.1
[1.1]: https://github.com/artizirk/wdisplays/compare/1.0...1.1
[1.0]: https://github.com/artizirk/wdisplays/releases/tag/1.0

[#11]: https://github.com/artizirk/wdisplays/pull/11
[#33]: https://github.com/artizirk/wdisplays/pull/33
[#34]: https://github.com/artizirk/wdisplays/pull/34
[#35]: https://github.com/artizirk/wdisplays/pull/35
[#37]: https://github.com/artizirk/wdisplays/pull/37
[#38]: https://github.com/artizirk/wdisplays/pull/38
[#5]: https://github.com/artizirk/wdisplays/issues/5
[#17]: https://github.com/artizirk/wdisplays/issues/17
[#18]: https://github.com/artizirk/wdisplays/issues/18
[#20]: https://github.com/artizirk/wdisplays/issues/20
[#22]: https://github.com/artizirk/wdisplays/issues/22
[#23]: https://github.com/artizirk/wdisplays/issues/23
[#24]: https://github.com/artizirk/wdisplays/issues/24
[#30]: https://github.com/artizirk/wdisplays/issues/30
[#31]: https://github.com/artizirk/wdisplays/issues/31
[#32]: https://github.com/artizirk/wdisplays/issues/32
