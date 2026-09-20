# Revised strategy translations

At PR head 3bb8243b6, all 800 revised values across 32 non-English languages are translated. Exactly 25 values per language changed: AI strategy and eight sets of Description, Summary and Profile. Keys, placeholders, paragraph breaks and unrelated translations remain intact. Existing strict catalog audit and all five translation tests pass without exemptions.

The bundled-font coverage test passes. Direct SDL2_ttf measurement at the game’s standard 13px font confirms all strategy-button labels fit its 110px text area; Finnish is widest at 108px. This checks width, not a native-speaker linguistic review. Existing English screenshots remain representative of layout.
