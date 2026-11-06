# Brawl Android

- Follow applicable parent AGENTS.md instructions and preserve the upstream history, credits, GPLv3 and third-party licenses.
- Keep the supplied ZIP, disc images, extracted game files, generated translations, engine blobs, saves, signing keys and device logs out of Git and any upload.
- Private game inputs and disc extraction belong under ../.tools/private-brawl; build output and artifacts are local and ignored.
- Publication of this port's source, tests and documentation to GenissonEmilio/Brawl-Android is authorized. Do not upload private game inputs or compiled artifacts, or modify the Galaxy/Futurama projects.
- Only the root agent operates the attached Android phone. Assign distinct source ownership before delegation; do not run overlapping builds or toolchain provisioning.
- Implement Smash controls in terms of real GameCube actions and verify simultaneous movement/jump/attack/shield/grab, release/cancel, analog thresholds and menus.
- Do not label an APK shell, launcher, synthetic scene or menu as a playable match. Report exactly what the native build and real-device tests demonstrate.
