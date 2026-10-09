# Standalone TJS fixtures do not link the runtime's plugin source list. Reuse
# its existing SHA-256 implementation for exact bytecode compatibility gates.
function(mikage_add_tjs_bytecode_hash target core)
    target_sources(${target} PRIVATE
        "${core}/plugins/Kirikiroid2/7zip/Sha256.c"
        "${core}/plugins/Kirikiroid2/7zip/Sha256Opt.c"
        "${core}/plugins/Kirikiroid2/7zip/CpuArch.c")
endfunction()
