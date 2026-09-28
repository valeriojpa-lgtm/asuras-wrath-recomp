# TU01 Native Build payload

Upload the verified TU01 generated archive to this directory using the exact name:

`ASURA_TU01_GENERATED.zip`

Expected SHA-256:

`29c9213ee11318b119dee60335b1dfe81f3dee740063c754b7967ac8c311b91a`

The GitHub Actions workflow verifies this hash, extracts `generated/default`, checks that 207 recompilation partitions are present, and builds only `asura_wrath_recomp` with `ASURA_SKIP_CODEGEN=ON`.

Do not commit retail XEX/XEXP material here.
