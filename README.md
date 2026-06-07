# JBRWayland

> Warning: this is slop code generated with AI. It is intended to be used by Echo and people who want to try the BetterWindow.

This fork is based on JetBrains Runtime 25 and prototypes direct
Skiko/Compose Desktop rendering on the Wayland AWT toolkit
(`sun.awt.wl.WLToolkit`).

The goal is to let Skiko render directly into a JBR-owned Wayland surface
instead of falling back to Swing/AWT graphics when running Compose Desktop
on Wayland.

## Changes In This Fork

- Adds Wayland support to the AWT JAWT drawing-surface path.
- Makes `JAWT_DrawingSurface.Lock` work under `WLToolkit`.
- Adds `JAWT_WaylandDrawingSurfaceInfo` with:
  - `wl_display`
  - JBR-owned render `wl_surface`
  - component surface bounds
  - Java logical bounds
  - integer Wayland buffer scale
  - fractional/effective UI scale
- Creates and reuses JBR-managed Wayland subsurfaces for direct native
  rendering.
- Lets the direct render subsurface be placed above the parent surface and
  moved/resized with the AWT component.
- Uses `wl_subsurface_set_desync` for the direct render surface.
- Allows JAWT callers to force early Wayland surface creation before the
  normal AWT show path.
- Fixes server-side Wayland decorations disappearing after fullscreen
  restore.
- Adds per-window JBR Wayland shadow control.
- Exposes fractional scale so Compose/Skiko can use `1.5x` instead of the
  rounded Wayland buffer scale `2x`.

The integer Wayland scale is still exposed for protocol/buffer-scale needs.
The fractional/effective scale is exposed separately for UI density and
render buffer sizing.

## Window Shadow Control

Set this root-pane client property on a window:

```java
window.getRootPane().putClientProperty("sun.awt.wl.WindowShadow", true);
```

Values:

- `true`: force the JBR Wayland shadow.
- `false`: disable the JBR Wayland shadow.
- unset: use the default behavior.

Transparent windows default to no JBR shadow unless the property is set to
`true`.

## Build JBR

Install the normal OpenJDK/JBR build dependencies. On Linux/Wayland you also
need Wayland development packages, including `wayland-protocols`,
`wayland-client`, and `xkbcommon` development headers.

Configure with a JDK 24 or JDK 25 boot JDK:

```bash
bash configure \
  --with-conf-name=skiko-wl \
  --with-debug-level=release \
  --with-jvm-variants=server \
  --disable-warnings-as-errors \
  --with-boot-jdk=/path/to/jdk-25 \
  --disable-javac-server \
  --enable-linkable-runtime \
  --enable-keep-packaged-modules
```

Build:

```bash
make CONF=skiko-wl jdk LOG=info
make CONF=skiko-wl images LOG=info
```

The exploded JDK is produced at:

```text
build/skiko-wl/images/jdk
```

Check it with:

```bash
build/skiko-wl/images/jdk/bin/java -version
```

## Create A Compact JRE

After building `images`, use the patched JDK's `jlink` and bundled `jmods`:

```bash
rm -rf /tmp/jbr-wayland-jre /tmp/jbr-wayland-jre-linux-x64.tar.gz

build/skiko-wl/images/jdk/bin/jlink \
  --module-path build/skiko-wl/images/jdk/jmods \
  --add-modules java.base,java.desktop,java.logging,java.management,jdk.unsupported,jdk.crypto.ec,java.naming,java.xml \
  --strip-debug \
  --no-header-files \
  --no-man-pages \
  --compress=zip-6 \
  --output /tmp/jbr-wayland-jre

tar -C /tmp -czf /tmp/jbr-wayland-jre-linux-x64.tar.gz jbr-wayland-jre
```

Check the JRE:

```bash
/tmp/jbr-wayland-jre/bin/java -version
```

The release asset should be named:

```text
jbr-wayland-jre-linux-x64.tar.gz
```

That name is intentionally boring: Gradle scripts can match it as a Linux
x64 JRE/runtime tarball from GitHub releases.

## Use With Compose Desktop

Use this JBR as the JVM that runs the app, and enable WLToolkit:

```bash
JAVA_HOME=/path/to/JBRWayland/build/skiko-wl/images/jdk ./gradlew :app:run
```

Add these JVM options to the Compose Desktop app:

```text
-Dawt.toolkit.name=WLToolkit
-Dcompose.application.configure.swing.globals=false
--enable-native-access=ALL-UNNAMED
--add-exports=java.desktop/sun.awt=ALL-UNNAMED
--add-opens=java.desktop/sun.awt.wl=ALL-UNNAMED
```

`compose.application.configure.swing.globals=false` matters for fractional
Wayland scale. Without it, Compose/Gradle packaging can force a Swing setup
path that rounds scale and breaks fullscreen sizing.

For Compose Gradle packaging, set the application `javaHome`:

```kotlin
compose.desktop {
    application {
        mainClass = "com.example.MainKt"
        javaHome = "/path/to/JBRWayland/build/skiko-wl/images/jdk"

        jvmArgs += listOf(
            "--enable-native-access=ALL-UNNAMED",
            "--add-exports=java.desktop/sun.awt=ALL-UNNAMED",
            "--add-opens=java.desktop/sun.awt.wl=ALL-UNNAMED",
        )
    }
}
```

```kotlin
fun main() {
    if (System.getenv("WAYLAND_DISPLAY") != null) {
        System.setProperty("awt.toolkit.name", "WLToolkit")
        System.setProperty("compose.application.configure.swing.globals", "false")
    }
    application {
    }
}
```

If you want Gradle `JavaExec` run tasks to always launch the app with this
JBR while Gradle itself runs on another stable JDK:

```kotlin
val patchedJbr = file("/path/to/JBRWayland/build/skiko-wl/images/jdk")

afterEvaluate {
    tasks.withType<JavaExec>().configureEach {
        setExecutable(patchedJbr.resolve("bin/java").absolutePath)
    }
}
```

Running Gradle itself on this patched JBR may expose Kotlin DSL/tooling
issues, so using JDK 21 for Gradle and this JBR for the app process is a
practical setup.

## Use Patched Skiko

This JBR-side patch needs a Skiko build that knows how to read
`JAWT_WaylandDrawingSurfaceInfo` and create a Wayland EGL surface from the
JBR-provided `wl_surface`.

One local development setup is:

```bash
cd /path/to/skiko
./gradlew :skiko:compileKotlinAwt \
  :skiko:linkJvmBindingsLinuxX64 \
  :skiko:publishToMavenLocal \
  -Pdeploy.version=0.148.1 \
  --no-daemon
```

Then substitute Compose's Skiko dependency to the locally published snapshot:

```kotlin
subprojects {
    configurations.configureEach {
        resolutionStrategy.dependencySubstitution {
            substitute(module("org.jetbrains.skiko:skiko"))
                .using(module("org.jetbrains.skiko:skiko:0.148.1-SNAPSHOT"))
            substitute(module("org.jetbrains.skiko:skiko-awt"))
                .using(module("org.jetbrains.skiko:skiko-awt:0.148.1-SNAPSHOT"))
            substitute(module("org.jetbrains.skiko:skiko-awt-runtime-linux-x64"))
                .using(module("org.jetbrains.skiko:skiko-awt-runtime-linux-x64:0.148.1-SNAPSHOT"))
        }
    }
}
```

Make sure `mavenLocal()` is present in dependency repositories.

## Expected Runtime Logs

With `-Dskiko.hardwareInfo.enabled=true`, a fractional `1.5x` Wayland setup
should report both scales:

```text
scale=2, effectiveScale=1.5
contentScale=1.5
800x600 -> buffer=1200x900
```

Here `scale=2` is the Wayland integer buffer scale, while
`effectiveScale=1.5` is the UI/render density used by Compose/Skiko.
