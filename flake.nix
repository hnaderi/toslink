{
  description = "toslink: one-way message link over Toslink for ESP-IDF";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    espidf.url = "github:hnaderi/espidf-nix";
  };

  outputs =
    {
      nixpkgs,
      flake-utils,
      espidf,
      ...
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        format-cpp = pkgs.writeShellApplication {
          name = "format-cpp";
          runtimeInputs = with pkgs; [ git findutils clang-tools ];
          text = "git ls-files -- '*.cpp' '*.c' '*.hpp' '*.h' | xargs clang-format -i";
        };
        format-cpp-check = pkgs.writeShellApplication {
          name = "format-cpp-check";
          runtimeInputs = with pkgs; [ git findutils clang-tools ];
          text = "git ls-files -- '*.cpp' '*.c' '*.hpp' '*.h' | xargs clang-format --dry-run --Werror";
        };
        example = "examples/link_test";
        fw-build = pkgs.writeShellScriptBin "fw-build" ''
          : ''${1:?Usage: fw-build <target>  (e.g. fw-build esp32s3)}
          idf.py -C ${example} -B "$PWD/build/$1" -D IDF_TARGET=$1 build
        '';
        fw-clean = pkgs.writeShellScriptBin "fw-clean" ''
          : ''${1:?Usage: fw-clean <target>  (e.g. fw-clean esp32s3)}
          idf.py -C ${example} -B "$PWD/build/$1" -D IDF_TARGET=$1 clean
        '';
        fw-flash = pkgs.writeShellScriptBin "fw-flash" ''
          : ''${1:?Usage: fw-flash <target>  (e.g. fw-flash esp32s3)}
          idf.py -C ${example} -B "$PWD/build/$1" -D IDF_TARGET=$1 flash
        '';
        fw-monitor = pkgs.writeShellScriptBin "fw-monitor" ''
          : ''${1:?Usage: fw-monitor <target>  (e.g. fw-monitor esp32s3)}
          idf.py -C ${example} -B "$PWD/build/$1" monitor
        '';
        fw-menuconfig = pkgs.writeShellScriptBin "fw-menuconfig" ''
          : ''${1:?Usage: fw-menuconfig <target>  (e.g. fw-menuconfig esp32s3)}
          idf.py -C ${example} -B "$PWD/build/$1" menuconfig
        '';
        test-host = pkgs.writeShellScriptBin "test-host" ''
          cmake -S test/host -B build/host && cmake --build build/host && ctest --test-dir build/host --output-on-failure
        '';
      in
      {
        packages = { inherit format-cpp format-cpp-check; };

        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            (espidf.lib.mkEspIdf {
              inherit pkgs;
              idfVersion = "v6.1";
            })
            cmake
            cmake-language-server
            tio

            format-cpp
            format-cpp-check
            fw-build
            fw-clean
            fw-flash
            fw-monitor
            fw-menuconfig
            test-host
          ];
        };
      }
    );
}
