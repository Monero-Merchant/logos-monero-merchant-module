{ pkgs, androidPkgs, delivery, abi, triple, cpu }:
let
  upstreamDeps = import (delivery + "/nix/deps.nix") { inherit pkgs; };
  deps = upstreamDeps // {
    libp2p = pkgs.applyPatches {
      name = "merchant-libp2p-address-snapshot";
      src = upstreamDeps.libp2p;
      patches = [ ./address-mapper-snapshot.patch ];
    };
  };
  paths = pkgs.lib.concatMapStringsSep " " (p: "--path:${p} --path:${p}/src --path:${p}/sds --path:${p}/segmentation") (builtins.attrValues (builtins.removeAttrs deps [ "nat_traversal" ]));
  sdk = (androidPkgs.androidenv.composeAndroidPackages {
    cmdLineToolsVersion = "9.0"; toolsVersion = "26.1.1"; platformToolsVersion = "34.0.5";
    buildToolsVersions = [ "34.0.0" ]; platformVersions = [ "23" ]; includeNDK = true;
    ndkVersion = "27.2.12479018"; includeCmake = false;
  }).androidsdk;
  hostTag = if pkgs.stdenv.hostPlatform.isDarwin then "darwin-x86_64" else "linux-x86_64";
in pkgs.stdenv.mkDerivation {
  pname = "merchant-logosdelivery-${abi}"; version = "6b1f3a09"; src = delivery;
  nativeBuildInputs = [ pkgs.nim-2_2 pkgs.gnumake pkgs.cmake pkgs.git ];
  dontUseCmakeConfigure = true; dontFixup = true;
  buildPhase = ''
    export HOME=$TMPDIR XDG_CACHE_HOME=$TMPDIR/.cache NIMBLE_DIR=$TMPDIR/.nimble
    ndk=${sdk}/libexec/android-sdk/ndk-bundle
    toolchain=$ndk/toolchains/llvm/prebuilt/${hostTag}
    export PATH=$toolchain/bin:$PATH
    cc=$toolchain/bin/${triple}28-clang
    cxx=$toolchain/bin/${triple}28-clang++
    nat=$TMPDIR/nat
    cp -R ${deps.nat_traversal} $nat
    chmod -R u+w $nat
    make -C $nat/vendor/miniupnp/miniupnpc CC=$cc AR=$toolchain/bin/llvm-ar CFLAGS="-Os -fPIC" build/libminiupnpc.a
    make -C $nat/vendor/libnatpmp-upstream CC=$cc AR=$toolchain/bin/llvm-ar CFLAGS="-Os -fPIC -DENABLE_STRNATPMPERR -DNATPMP_MAX_RETRIES=4" libnatpmp.a
    mkdir -p build library/generated
    # logos.dev has no RLN requirement. RLN deployments require a separate
    # Android membership/proof provider; never pretend those networks are ready.
    nim c ${paths} --path:$nat --path:$nat/src --path:. \
      --os:android --cpu:${cpu} --cc:clang --clang.path:$toolchain/bin \
      --clang.exe:${triple}28-clang --clang.linkerexe:${triple}28-clang++ \
      --debugger:off --threads:on --mm:refc --app:lib --noMain --opt:size --nimMainPrefix:liblogosdelivery \
      -d:androidNDK -d:disable_rln -d:disable_libbacktrace -d:chronosEventEngine=epoll \
      -d:metrics -d:discv5_protocol_id=d5waku -d:chronicles_log_level=ERROR \
      -d:libp2p_mix_experimental_exit_is_dest \
      -d:LeopardExtraCompilerFlags=-fno-openmp -d:LeopardExtraLinkerFlags=-fno-openmp \
      "-d:LeopardCmakeFlags=-DCMAKE_BUILD_TYPE=Release -DENABLE_OPENMP=off -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCOMPILER_SUPPORTS_MARCH_NATIVE=FALSE -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_C_COMPILER=$cc -DCMAKE_CXX_COMPILER=$cxx -DCMAKE_CXX_FLAGS=${if cpu == "amd64" then "-mssse3" else "-DANDROID"}" \
      --passL:-llog --passL:-lc++_shared --passL:-Wl,-z,max-page-size=16384 --passL:-Wl,-z,common-page-size=16384 \
      -d:ffiGenBindings -d:targetLang=c -d:ffiOutputDir=generated -d:ffiSrcPath=../liblogosdelivery.nim \
      --nimcache:$TMPDIR/nimcache --out:build/liblogosdelivery.so library/liblogosdelivery.nim
  '';
  installPhase = ''
    mkdir -p $out/lib $out/include/generated $out/include/tinycbor
    cp build/liblogosdelivery.so $out/lib/
    cp $toolchain/sysroot/usr/lib/${triple}/libc++_shared.so $out/lib/
    cp library/*.h $out/include/
    cp library/generated/*.h $out/include/generated/
    cp ${deps.ffi}/ffi/codegen/templates/cpp/vendor/tinycbor/*.[ch] $out/include/tinycbor/
  '';
}
