# Sigil Siege (Nintendo 3DS)

A wave-based roguelite where you **draw runes on the touch screen** to kill monsters on the top screen.

- Every enemy carries a chain of runes above its head, e.g. `triangle -> vertical line -> horizontal line`.
- Draw the **first** rune (one stroke) and it strikes the nearest enemy that needs it. Clear the whole chain to kill it.
- Enemies that reach your wizard hurt you. Wrong runes break your combo and shove an enemy closer.
- Every wave is bigger, faster and uses more rune types. After each wave pick 1 of 3 upgrades.

Runes: `|` `-` triangle, circle, square, `/` `\`, zigzag (Z / N / W). New runes unlock as waves go on.

Controls: stylus/finger on the bottom screen to draw and to tap upgrade cards. D-pad up/down + A also works on the upgrade screen. START quits.

Works on Old 3DS/2DS and New 3DS/2DS (New models run at full clock speed).

## Upgrades

Heart Crystal, Frost Aura, Chain Lightning, Resonance, Frost Nova, Healing Draught, Soul Siphon, Rune Shield, Steady Hand.

## Get the .cia without installing anything (GitHub Actions)

1. Create a free GitHub account and a new repository.
2. Upload everything in this folder (including the hidden `.github` folder) to the repository.
3. Open the **Actions** tab -> *Build 3DS* -> wait for the green check.
4. Open the finished run and download the **SigilSiege** artifact. It contains `SigilSiege.cia` and `SigilSiege.3dsx`.
5. Copy the `.cia` to your SD card and install it with FBI, or put the `.3dsx` in `/3ds/` and run it from the Homebrew Launcher.

## Build locally

Install [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the 3DS toolchain:

    dkp-pacman -S 3ds-dev

then:

    make          # SigilSiege.3dsx
    make cia      # SigilSiege.cia  (needs makerom and bannertool on your PATH)
    make test     # runs the recognizer + wave-balance tests on your PC

Or with Docker:

    docker run --rm -v "$PWD":/src -w /src devkitpro/devkitarm bash -c "dkp-pacman -S --noconfirm 3ds-dev && make"

## Layout

    source/shapes.c   single-stroke rune recognizer (pure C)
    source/game.c     waves, enemies, upgrades, scoring (pure C)
    source/render.c   citro2d drawing for both screens
    source/main.c     3DS init, input, main loop, high-score file
    resources/        icon, banner, jingle, CIA descriptor (cia.rsf)
    tests/            PC-side tests and API stubs for syntax checks

Your best score is saved to `sdmc:/3ds/SigilSiege/score.sav`.
