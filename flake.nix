{
  description = "melonDS fork dev shell (BGM-at-1x research)";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in {
      devShells.${system}.default = pkgs.mkShell {
        inputsFrom = [ pkgs.melonDS ];
        packages = with pkgs; [ cmake ninja pkg-config python3 gdb ];
      };
    };
}
