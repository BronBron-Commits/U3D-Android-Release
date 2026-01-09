# U3D-Android-Release

A native Android **OpenGL ES 2.0** 3D prototype built directly on the **Android NDK** using **GameActivity**, with a fully manual rendering pipeline and minimal abstractions. This project serves as the Android “release” counterpart to the X11-based U3D prototypes, preserving core logic while adapting it to Android’s lifecycle and input model.

---

## Overview

U3D-Android-Release is intentionally **engine-free**. It demonstrates how to:

- Stand up an EGL + OpenGL ES 2.0 pipeline manually
- Drive rendering from `android_main` using `native_app_glue`
- Bridge Java/Kotlin touch input into native code via JNI
- Implement rotation with inertia and damping
- Perform all matrix math by hand (no GLM, no engine math libs)
- Maintain a clean separation between core rendering logic and platform glue

The goal is clarity, control, and portability—not feature completeness.

---

## Features

- **Native GameActivity setup**
  - Uses `GameActivity` and `android_native_app_glue`
  - No SurfaceView or GLSurfaceView abstractions

- **Manual EGL lifecycle**
  - Explicit display, surface, and context creation
  - OpenGL ES 2.0 context targeting broad device support

- **Shader-based rendering**
  - Minimal vertex and fragment shaders
  - Per-vertex color interpolation
  - Single VBO, triangle-based cube geometry

- **Hand-rolled math**
  - Identity, rotation, translation, perspective matrices
  - Explicit matrix multiplication
  - No external math dependencies

- **Touch-driven rotation with inertia**
  - JNI bridge from Java/Kotlin to native
  - Velocity accumulation, damping, and free-spin behavior

- **Depth testing and back-face culling**
  - Correct 3D spatial rendering
  - Simple but correct render state

---

## Controls

- **Touch drag**
  - Horizontal drag → Y-axis rotation
  - Vertical drag → X-axis rotation
- Rotation continues with inertia and gradually slows via damping

---

## Project Structure

U3D-Android-Release/ ├── app/ │   ├── src/main/ │   │   ├── cpp/ │   │   │   └── main.cpp        # Native entry point, rendering, math │   │   ├── java|kotlin/ │   │   │   └── MainActivity    # Touch input → JNI bridge │   │   └── AndroidManifest.xml │   └── CMakeLists.txt ├── gradle/ ├── build.gradle.kts ├── settings.gradle.kts └── README.md

---

## Build Requirements

- Android Studio (Giraffe or newer recommended)
- Android NDK (r25+)
- CMake (bundled with Android Studio)
- OpenGL ES 2.0–capable Android device or emulator

---

## Building and Running

1. Open the project in **Android Studio**
2. Ensure the NDK is installed:
   - `SDK Manager → SDK Tools → Android NDK`
3. Select a physical Android device (recommended)
4. Build and run normally (`Run ▶`)

No additional configuration is required.

---

## Relationship to Other Projects

This repository is part of a larger workflow:

- **U3D-Android-X11**
  - Fast iteration and experimentation
  - Desktop X11 environment
  - Canonical logic and structure

- **U3D-Android-Release**
  - Android-native deployment
  - Lifecycle and input adaptation
  - Minimal, mechanical porting from X11

The X11 prototype is where ideas are explored.
This repository is where they are **stabilized and shipped**.

---

## Design Philosophy

- Prefer explicit code over abstraction
- Treat Android as a deployment target, not a design environment
- Keep platform glue thin and obvious
- Avoid hidden state and implicit behavior
- Make every system understandable by reading a single file

This project is meant to be read, modified, and extended.

---

## License

MIT License. See `LICENSE` for details.
