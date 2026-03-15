---
description: Push to GitHub - regenerate song index, build, commit and push
---

## Steps

1. Regenerate `songs/index.json` from `.ahk` files in `songs/` directory:
// turbo
```bash
bash /home/tony/Dev/serenade/generate_song_index.sh
```

2. Build the DLL to ensure everything compiles:
// turbo
```bash
cmake --build /home/tony/Dev/serenade/build --target Serenade
```

3. Stage all changes:
```bash
git -C /home/tony/Dev/serenade add -A
```

4. Review staged changes:
// turbo
```bash
git -C /home/tony/Dev/serenade status
```

5. Commit with an appropriate message (ask user or infer from changes).

6. Push to origin main:
```bash
git -C /home/tony/Dev/serenade push origin main
```

7. Remind the user to redeploy `build/Serenade.dll` if any C++ files changed.
