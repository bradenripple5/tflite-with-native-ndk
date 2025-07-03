plugins {
//    alias(libs.plugins.android.application)
//    alias(libs.plugins.kotlin.android)
    id("com.android.application")
    kotlin("android")
}

android {
    namespace = "com.example.ndkcamera"
    compileSdk  = 35

    defaultConfig {
        applicationId = "com.example.ndkcamera"
        minSdk        = 24
        targetSdk     = 35
        versionCode   = 1
        versionName   = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        /* ---------- this is the RIGHT scope for cppFlags / abiFilters ---------- */
        externalNativeBuild {
            cmake {
                cppFlags    += listOf("-frtti", "-fexceptions")
                abiFilters  += listOf("armeabi-v7a", "arm64-v8a")

            }
        }

    }

    android {
        // ... existing config ...

        signingConfigs {
            getByName("debug").apply {
                storeFile = file("${System.getProperty("user.home")}/.android/debug.keystore")
                storePassword = "android"
                keyAlias = "androiddebugkey"
                keyPassword = "android"
            }
        }

        buildTypes {
            debug {
                signingConfig = signingConfigs.getByName("debug")
            }
            release {
                isMinifyEnabled = false
                proguardFiles(
                    getDefaultProguardFile("proguard-android-optimize.txt"),
                    "proguard-rules.pro"
                )
            }
        }

        // ... rest of the block ...
    }


    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    kotlinOptions { jvmTarget = "11" }

    /* ---------- top-level externalNativeBuild: only path + version ---------- */
    externalNativeBuild {
        cmake {
            path    = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildFeatures { viewBinding = true }
}

dependencies {
    implementation(libs.androidx.appcompat)


    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    implementation(libs.material)
    implementation(libs.androidx.constraintlayout)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)

    // TensorFlow Lite dependency
//    implementation ("org.tensorflow:tensorflow-lite:2.7.0") // Replace with the desired version

////    implementation("org.tensorflow:tensorflow-lite-c:2.12.0") // only available up to 2.12.0
//    implementation("org.tensorflow:tensorflow-lite-gpu:2.14.0")
//    implementation("org.tensorflow:tensorflow-lite:2.14.0") // or latest version

//    implementation("org.tensorflow:tensorflow-lite:2.14.0")
}
