#define SET_FRAME 0
#define SET_VIEW 1
#define SET_MATERIAL 2
#define SET_DRAW 3

struct Light {
    vec3 pos;
    int type; // 0 = point light, 1 = spot light, 2 = directional light
    vec3 dir;
    float intensity;
    vec3 color;
    float innerCutOff;
    float outerCutOff;
};

#define MAX_NUM_LIGHTS 100
layout (set = SET_FRAME, binding = 0) uniform FrameData {
    int uNumLights;
    Light uLights[MAX_NUM_LIGHTS];
    int uVisualizedGBufferAttachmentIndex;
    int uEnableToneMapping;
    float uExposure;
};

layout (set = SET_FRAME, binding = 1) uniform sampler uSamplers[2];
#define SMP_NEAREST 0
#define SMP_LINEAR  1

layout (set = SET_FRAME, binding = 2) uniform texture2D uGbuffer[5];
#define TEX_G_POSITION    0
#define TEX_G_NORMAL      1
#define TEX_G_ALBEDO      2
#define TEX_G_MRAH        3
#define TEX_G_MATINDEX    4

layout (set = SET_FRAME, binding = 3) uniform texture2D uHDRBuffer;

layout (set = SET_VIEW, binding = 0) uniform ViewData {
    mat4 uViewMat;
    mat4 uProjMat;
    vec3 uViewPos;
    int uEnableNormalMap;
};

layout (set = SET_MATERIAL, binding = 0) uniform texture2D uMaterialTextures[6];
#define TEX_ALBEDO    0
#define TEX_METALLIC  1
#define TEX_ROUGHNESS 2
#define TEX_AO        3
#define TEX_NORMAL    4
#define TEX_HEIGHT    5