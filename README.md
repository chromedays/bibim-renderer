# Bibim Renderer

## by Team Bab

### Summary

Bibim Renderer is renderer developed with Vulkan 1.2, supporting :

- Parameters controled by GUI.
- Scene based render management.
- Instanced rendering.
- Image based lighting.
- Physically based render.
- HDR.
- Deferred rendering.
- Forward rendering.
- Various light types, include point, spot, and directional lights.

### How to build

Before compile, make sure to install VulkanSDK 1.2 first.
To compile Bibim Renderer :

1. Generate config file by running gen_config.py script on Python3. Run "python3 gen_config.py".
2. Run "./Fbuild.exe Debug" for debug mode, "./Fbuild.exe Release". for release mode.
3. Excutables will generated in "bin" directory if building was successful.

### How to use

To look around, simply click and drag on application window.
To move around, use W for moving forward, S for backward, A for left, D for right.

When you first launch the application, it will initially render the Crytek Sponza scene.
To change scenes, use the "Scene" GUI window. Also, GUIs are stacked on same spot initially, you can move and resize GUI windows on your own.
To change render mode between forward render and deferred render and visualize deferred rendering buffers, use the "Render Setting" GUI window.
To endable/disable normal mapping, tone mapping and TBN visualization, or adjust exposure values for tone mapping, use "Settings" GUI window.

To change light settings in Sponza scene, use "Sponza" GUI window.

To select materials for balls in ShaderBall scene, use "Material Selector" GUI window.
To preview materials for balls in ShaderBall scene, use "Current Material" GUI window.
