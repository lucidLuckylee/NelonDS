{
  description = "Build environment for melonDS-android";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
        config = {
          allowUnfree = true;
          android_sdk.accept_license = true;
        };
      };

      # Versions from melonDS-android: buildSrc/src/main/kotlin/AppConfig.kt and app/build.gradle.kts
      buildToolsVersion = "37.0.0";
      androidComposition = pkgs.androidenv.composeAndroidPackages {
        platformVersions = [ "37.0" ];
        buildToolsVersions = [ buildToolsVersion "36.0.0" ];
        includeNDK = true;
        ndkVersions = [ "28.0.13004108" ];
        cmakeVersions = [ "3.22.1" ];
        includeEmulator = false;
        includeSystemImages = false;
        includeSources = false;
      };
      androidSdk = androidComposition.androidsdk;
      sdkRoot = "${androidSdk}/libexec/android-sdk";
      jdk = pkgs.jdk21;
    in {
      devShells.${system}.default = pkgs.mkShell {
        packages = [ jdk androidSdk pkgs.git pkgs.unzip ];
        JAVA_HOME = jdk.home;
        ANDROID_HOME = sdkRoot;
        ANDROID_SDK_ROOT = sdkRoot;
        ANDROID_NDK_ROOT = "${sdkRoot}/ndk/28.0.13004108";
        # The aapt2 jar AGP fetches from Maven is a generic-linux binary that cannot run on NixOS
        GRADLE_OPTS = "-Dorg.gradle.project.android.aapt2FromMavenOverride=${sdkRoot}/build-tools/${buildToolsVersion}/aapt2";
      };
    };
}
