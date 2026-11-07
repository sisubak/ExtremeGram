{
  description = "Yurigram";
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    self.submodules = true;
  };

  outputs = { self, nixpkgs }:
    let
      lib = nixpkgs.lib;
      systems = [
        "aarch64-linux"
        "x86_64-linux"
      ];
      forAllSystems = lib.genAttrs systems;
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        rec {
          yurigram = pkgs.callPackage ./default.nix { };
          default = yurigram;
        }
      );

      apps = forAllSystems (
        system:
        let
          package = self.packages.${system}.yurigram;
          app = {
            type = "app";
            program = lib.getExe package;
            meta = package.meta;
          };
        in
        {
          default = app;
          yurigram = app;
        }
      );

      formatter = forAllSystems (
        system: nixpkgs.legacyPackages.${system}.nixfmt
      );
    };
}
