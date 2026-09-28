# Asura's Wrath DLC Lab

Read-only STFS audit tooling. This branch is intentionally separate from TU01.

## Use
1. Put the five original Marketplace packages in `Data/DLC/`.
2. Run `tools\\INSPECT_DLC.cmd`.
3. The tool writes `DLC_REPORT.json` and `DLC_REPORT.csv`.

It reads only the fixed STFS header and does not extract, install, mount, modify, or license DLC.

Expected Title ID: `43430817`.
Expected content type: `0x00000002` (MarketplaceContent).

No DLC integration should be merged into TU01 until TU01 Native Boot / Gameplay passes.
