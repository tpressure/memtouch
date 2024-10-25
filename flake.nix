{
  description = "memtouch";

  inputs = {
    argparse.url = "github:p-ranav/argparse/master";
    argparse.flake = false;
    nixpkgs.url = "github:nixos/nixpkgs/nixpkgs-unstable";
  };

  outputs =
    {
      self,
      argparse,
      nixpkgs,
    }:
    let
      pkgs = nixpkgs.legacyPackages.x86_64-linux;
    in
    {
      devShells.x86_64-linux.default = pkgs.mkShell {
        inputsFrom = [ self.packages.x86_64-linux.memtouch ];
        packages = with pkgs; [
          nixfmt-rfc-style
        ];
      };

      formatter.x86_64-linux = pkgs.nixfmt-rfc-style;

      packages.x86_64-linux =
        let
          memtouch = pkgs.stdenv.mkDerivation {
            name = "memtouch";
            version = "0.1.0";
            src = pkgs.nix-gitignore.gitignoreSource [ ] ./.;
            # TODO fetching the git submodule could probably also be done
            # more naturally.
            preConfigure = ''
              mkdir -p contrib/argparse
              cp -r ${argparse}/. contrib/argparse
            '';
            makeFlags = [
              "DESTDIR=$(out)"
            ];
          };
        in
        {
          inherit memtouch;
          default = memtouch;
        };
    };
}
