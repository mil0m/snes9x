# Corruption UI icons

Tabler Icons, outline set, version 3.31.0 (MIT, see LICENSE), from
`https://cdn.jsdelivr.net/npm/@tabler/icons@3.31.0/icons/outline/<name>.svg`.

They keep `stroke="currentColor"`: `ui::icon()` (qt/src/UiKit.cpp) swaps in the palette's
text colour when it draws them, so one copy serves light, dark and disabled. To add one,
download it from the same version, then list it in `../corruption_icons.qrc` and in `kIcons`
in UiKit.cpp (a test renders every name).
