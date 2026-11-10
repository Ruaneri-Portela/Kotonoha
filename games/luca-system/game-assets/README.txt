# LucaSystem local game assets

Commercial LucaSystem game data (e.g. AIR, Kanon, CLANNAD) is NOT part of this
repository and must not be committed here. Supply your own legally obtained
data:

  games/luca-system/game-assets/PARAM.PAK
  games/luca-system/game-assets/SCRIPT.PAK
  games/luca-system/game-assets/SYSSE.PAK
  games/luca-system/game-assets/*.PAK   (image archives, etc.)

Then run:

  build/games/luca-system/LucaSystem.app/Contents/MacOS/LucaSystem \
    --luca-system games/luca-system/game-assets --no-launcher

Or use the folder launcher (`--launcher`, default) to point at your AIR
installation (the Steam reference test used
/Volumes/Nero 2TB/SteamLibrary/steamapps/common/AIR/files).

This port does not download or supply commercial game data.
