{
  description = "A graphical application for configuring displays in Wayland compositors";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
  };

  outputs = inputs@{ self, nixpkgs, flake-parts }: flake-parts.lib.mkFlake { inherit inputs; } {
    systems = [ "x86_64-linux" "aarch64-linux" ];
    perSystem = { pkgs, system, ... }: {
      packages = rec {
        wdisplays = pkgs.stdenv.mkDerivation {
          pname = "wdisplays";
          version = "git";

          src = pkgs.lib.cleanSource ./.;

          nativeBuildInputs = with pkgs; [ meson ninja pkg-config wrapGAppsHook ];

          buildInputs = with pkgs; [ gtk3 libepoxy wayland ];

          meta = with pkgs.lib; {
            description = "A graphical application for configuring displays in Wayland compositors";
            homepage = "https://github.com/cyclopsian/wdisplays";
            license = licenses.gpl3Plus;
            platforms = platforms.linux;
            mainProgram = "wdisplays";
          };
        };
        default = wdisplays;
      };
    };
  };
}
