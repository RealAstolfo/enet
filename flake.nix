{
  description = "enet — C++20 Networking Library with I2P, HTTP, DHT support";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    exstd = {
      url = "github:RealAstolfo/exstd";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.flake-utils.follows = "flake-utils";
    };
    i2pd-src = { url = "github:PurpleI2P/i2pd"; flake = false; };
  };

  outputs = { self, nixpkgs, flake-utils, exstd, i2pd-src }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        exstdPkg = exstd.packages.${system}.default;
      in {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "enet";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = with pkgs; [ gcc gnumake pkg-config ];
          buildInputs = with pkgs; [ openssl zlib boost libmd ];

          postUnpack = ''
            rm -rf $sourceRoot/vendors
            mkdir -p $sourceRoot/vendors
            cp -r ${exstdPkg.passthru.src-with-vendors} $sourceRoot/vendors/exstd
            cp -r ${i2pd-src} $sourceRoot/vendors/i2pd
            chmod -R u+w $sourceRoot/vendors
          '';

          buildPhase = ''
            make i2p.o
          '';

          installPhase = ''
            mkdir -p $out/include $out/lib
            cp -r include/* $out/include/
            cp i2p.o $out/lib/ 2>/dev/null || true
          '';

          passthru.src-with-vendors = pkgs.runCommand "enet-src" {} ''
            cp -r ${self} $out
            chmod -R u+w $out
            rm -rf $out/vendors
            mkdir -p $out/vendors
            cp -r ${exstdPkg.passthru.src-with-vendors} $out/vendors/exstd
            cp -r ${i2pd-src} $out/vendors/i2pd
            chmod -R u+w $out/vendors
          '';
        };

        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            gcc
            gnumake
            pkg-config
            openssl
            zlib
            boost
            libmd
          ];

          shellHook = ''
            if [ ! -d vendors/exstd ] || [ -L vendors/exstd ]; then
              rm -rf vendors/exstd
              mkdir -p vendors
              cp -r ${exstdPkg.passthru.src-with-vendors} vendors/exstd
              chmod -R u+w vendors/exstd
            fi
            if [ ! -d vendors/i2pd ] || [ -L vendors/i2pd ]; then
              rm -rf vendors/i2pd
              mkdir -p vendors
              cp -r ${i2pd-src} vendors/i2pd
              chmod -R u+w vendors/i2pd
            fi
            echo "enet development environment"
            echo "  Build: make all"
          '';
        };
      }
    );
}
