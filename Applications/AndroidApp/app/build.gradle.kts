plugins {
    alias(libs.plugins.android.application)
}

val syncEngineAssets by tasks.registering(Copy::class) {
    from(file("../../../Res")) {
        include("pipelines/**")
        include("shaders/**")
        include("snippets/**")
        include("shadergraph/**")
        include("scenes/**")
        include("icons/**")
        include("scripts/dist/**")
        include("scripts/js/**")
        include("ball.mtl")
        include("ball.obj")
        include("mesh.png")
        include("models/floor_1.glb")
    }
    into(layout.buildDirectory.dir("generated/assets/engineRes/res"))
}

tasks.named("preBuild") {
    dependsOn(syncEngineAssets)
}

tasks.matching { it.name.startsWith("merge") && it.name.endsWith("JniLibFolders") }.configureEach {
    dependsOn(tasks.matching { it.name.startsWith("configureCMake") || it.name.startsWith("buildCMake") })
}

android {
    namespace = "com.example.androidtutorial"
    compileSdk = 35
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "com.example.androidtutorial"
        minSdk = 31
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++20"
                arguments += "-DANDROID_STL=c++_shared"
                arguments += "-DVCPKG_MANIFEST_MODE=OFF"
                arguments += "-DCMAKE_TOOLCHAIN_FILE=${System.getenv("VCPKG_ROOT")?.replace("\\", "/")}/scripts/buildsystems/vcpkg.cmake"
                targets += "AndroidApp"
            }
        }

        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }

    sourceSets {
        getByName("main") {
            assets.srcDir(layout.buildDirectory.dir("generated/assets/engineRes"))
            jniLibs.srcDir(file("../../../FCT/FCT_Node/.cache/jniLibs"))
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
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    externalNativeBuild {
        cmake {
            path = file("../../../CMakeLists.txt")
            version = "3.22.1"
        }
    }
    buildFeatures {
        viewBinding = true
    }
}

dependencies {
    implementation(libs.appcompat)
    implementation(libs.material)
    implementation(libs.constraintlayout)
    testImplementation(libs.junit)
    androidTestImplementation(libs.ext.junit)
    androidTestImplementation(libs.espresso.core)
}
