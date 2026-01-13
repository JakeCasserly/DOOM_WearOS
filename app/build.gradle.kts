plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "com.example.doom_wearos"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.example.doom_wearos"
        minSdk = 34
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"


        externalNativeBuild {
            cmake {
                // In Kotlin, you use setOf() or addAll() for abiFilters
                abiFilters.addAll(listOf("armeabi-v7a", "arm64-v8a", "x86", "x86_64"))

                // You can also use setOf:
                // abiFilters += setOf("armeabi-v7a", "arm64-v8a", "x86", "x86_64")

                arguments += "-DANDROID_PLATFORM=android-34"
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
        applicationVariants.all {
            val variant = this
            variant.outputs.all {
                val output = this as com.android.build.gradle.internal.api.BaseVariantOutputImpl
                val builtType = variant.buildType.name
                output.outputFileName = "Doom-${builtType}.apk"
            }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    kotlinOptions {
        jvmTarget = "11"
    }
    useLibrary("wear-sdk")
    buildFeatures {
        compose = true
    }
    externalNativeBuild {
        cmake {
            version = "3.22.1"
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }
}

dependencies {

    implementation(libs.play.services.wearable)
    implementation(platform(libs.compose.bom))
    implementation(libs.ui)
    implementation(libs.ui.graphics)
    implementation(libs.ui.tooling.preview)
    implementation(libs.compose.material)
    implementation(libs.compose.foundation)
    implementation(libs.wear.tooling.preview)
    implementation(libs.activity.compose)
    implementation(libs.core.splashscreen)
    implementation(libs.startup.runtime)
    androidTestImplementation(platform(libs.compose.bom))
    androidTestImplementation(libs.ui.test.junit4)
    debugImplementation(libs.ui.tooling)
    debugImplementation(libs.ui.test.manifest)
}