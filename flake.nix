{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-23.11";
    nixgl.url = "github:nix-community/nixGL";
    nixgl.inputs.nixpkgs.follows = "nixpkgs";
  };
  outputs = {self, nixpkgs, nixgl}:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
        overlays = [ nixgl.overlay ];
        config = { allowUnfree = true; };
      };
    in
    {
      packages.${system} = {
        tangerine = pkgs.stdenv.mkDerivation {
          name = "tangerine";
          patches = [
            (pkgs.fetchpatch {
              url = "https://github.com/Aeva/tangerine/pull/12/commits/2d7d1ae1e21e8fe52df2c4a33e947b2ff6b07812.patch";
              hash = "sha256-zLAx5FOvtUsUZM/nUCFW8Z1Xe3+oV95Nv1s3GaNcV/c=";
            })
          ];
          nativeBuildInputs = [ pkgs.cmake ];
          buildInputs = with pkgs; [ SDL2 ncurses ];
          src = ./.;
          inherit system;
        };
        default = pkgs.writeScriptBin "tangerine"
          ''
          ${pkgs.nixgl.nixGLIntel}/bin/nixGLIntel ${self.packages.${system}.tangerine}/bin/tangerine
          '';
      };
      devShells.${system}.default = pkgs.mkShell {
        buildInputs = with pkgs; [ cmake SDL2 ncurses pkgs.nixgl.nixGLIntel ];
        shellHook = ''
          export LD_LIBRARY_PATH=$(nixGLIntel printenv LD_LIBRARY_PATH):$LD_LIBRARY_PATH
        '';
      };
    };
}
