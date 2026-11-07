# ExtremeGram: build status

This private fork is based on Revincx/Yurigram. It contains an experimental, unverified change for an online-status toggle and partial rebranding. The change has not yet been compiled or tested against Telegram. Server rate limits, privacy settings, other sessions, and shutdown behavior may affect the status seen by others. The app still uses upstream Linux application identifiers and paths.

## Build

Run the **Build ExtremeGram AppImage** workflow manually in GitHub Actions. It requires repository Actions secrets `TDESKTOP_API_ID` and `TDESKTOP_API_HASH`. The AppImage and SHA-256 checksum appear as workflow artifacts if the job succeeds. Artifacts expire after 30 days. The workflow does not back up `out`: it contains generated build files and may contain credentials.

AppImage builds use the upstream static build environment and may still require system libraries. A successful compile is not a substitute for testing login, toggling, and launch on Kali.

The project is GPL-licensed; upstream copyright and license files remain in the repository.
