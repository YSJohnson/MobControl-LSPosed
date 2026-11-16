pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.PREFER_SETTINGS)
    repositories {
        google()
        mavenCentral()
        // Xposed API (LSPosed 兼容)
        maven { url = uri("https://api.xposed.info/") }
    }
}

rootProject.name = "MobCtrl"
include(":app")
