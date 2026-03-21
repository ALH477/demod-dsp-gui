{
  description = "DeMoDOOM — DOOM-style Faust DSP controller with PipeWire";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "demodoom";
          version = "0.2.0";
          src = ./.;
          nativeBuildInputs = with pkgs; [ cmake pkg-config ];
          buildInputs = with pkgs; [ SDL2 pipewire rtmidi libjack2 nlohmann_json ffmpeg-headless ];
          cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" ];
          meta = with pkgs.lib; {
            description = "DOOM-style framebuffer GUI for Faust DSP with multi-input and multi-resolution";
            license = licenses.gpl3Only;
            platforms = platforms.linux;
            mainProgram = "demodoom";
          };
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.default ];
          packages = with pkgs; [
            gdb valgrind clang-tools bear faust rtmidi libjack2 nlohmann_json catch2_3 ffmpeg-headless
          ];
          shellHook = ''
            echo "┌──────────────────────────────────┐"
            echo "│  DeMoDOOM Dev Shell               │"
            echo "│  cmake -B build && make -C build  │"
            echo "└──────────────────────────────────┘"
            export CMAKE_EXPORT_COMPILE_COMMANDS=ON
          '';
        };
      }
    );
}
