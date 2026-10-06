# Writes OUT with the current build time; run on every build by natalia_build_info.
string(TIMESTAMP NATALIA_BUILD_TIME "%Y-%m-%d %H:%M:%S")
file(WRITE "${OUT}" "#define NATALIA_BUILD_TIME \"${NATALIA_BUILD_TIME}\"\n")
