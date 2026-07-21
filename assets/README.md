# assets

`icon.svg` is the source of truth for the app icon; `icon.png` (1024×1024,
transparent) is what CMake feeds JUCE's `ICON_BIG`, which builds the
bundle's `.icns`.

Regenerate the PNG after editing the SVG — note it must keep its alpha
channel, which rules out `qlmanage` (it composites thumbnails onto
white). Headless Chrome preserves it:

```
cd assets
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
    --headless=new --disable-gpu --hide-scrollbars \
    --window-size=1024,1024 --default-background-color=00000000 \
    --screenshot="$PWD/icon.png" "file://$PWD/icon.svg"
```
