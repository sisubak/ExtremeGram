{ lib, telegram-desktop }:

let
  versionLine = lib.findFirst
    (line: lib.hasPrefix "AppVersionStrSmall " line)
    (throw "AppVersionStrSmall is missing from Telegram/build/version")
    (lib.splitString "\n" (builtins.readFile ./Telegram/build/version));
  version = lib.removePrefix "AppVersionStrSmall " versionLine;
in
telegram-desktop.override {
  pname = "yurigram";
  unwrapped = telegram-desktop.unwrapped.overrideAttrs (old: {
    pname = "yurigram-unwrapped";
    inherit version;
    src = ./.;
    meta = old.meta // {
      description = "Yurigram, a Telegram Desktop client";
      mainProgram = "Telegram";
    };
  });
}
