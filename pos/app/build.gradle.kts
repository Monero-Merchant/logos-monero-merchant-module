plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}
android {
    namespace = "org.moneromerchant.pos"
    compileSdk = 37
    ndkVersion = "27.2.12479018"
    providers.gradleProperty("pos.ndkPath").orNull?.let { ndkPath = it }
    defaultConfig {
        applicationId = "org.moneromerchant.pos.poc"
        minSdk = 28
        targetSdk = 36
        versionCode = 5
        versionName = "0.2.0"
        ndk { abiFilters += providers.gradleProperty("pos.abis").getOrElse("arm64-v8a,x86_64").split(",") }
        externalNativeBuild { cmake { arguments += "-DANDROID_STL=c++_shared" } }
    }
    buildFeatures {
        compose = true
        buildConfig = true
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    externalNativeBuild { cmake { path = file("../native/CMakeLists.txt"); version = providers.gradleProperty("pos.cmakeVersion").getOrElse("3.22.1") } }
    packaging { jniLibs.pickFirsts += "**/libc++_shared.so" }
}
dependencies {
    implementation(platform("androidx.compose:compose-bom:2026.09.00"))
    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.ui:ui-tooling-preview")
    debugImplementation("androidx.compose.ui:ui-tooling")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.10.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.10.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.10.2")
    implementation("org.bouncycastle:bcprov-jdk18on:1.83")
    implementation("com.journeyapps:zxing-android-embedded:4.3.0")
}
