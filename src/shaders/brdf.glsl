
const float PI = 3.1415926535897932384626433832795;

// Trowbridge-Reitz GGX
float distributionGGX(vec3 N, vec3 H, float a) {
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = NdotH2 * (a2 - 1) + 1;
    denom = PI * denom * denom;

    return num / denom;
}

float geometrySchlickGGX(float NdotV, float k) {
    float num = NdotV;
    float denom = NdotV * (1 - k) + k;
    return num / denom;
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float k) {
    float NdotV = max(dot(N, V), 0);
    float NdotL = max(dot(N, L), 0);
    float ggx1 = geometrySchlickGGX(NdotV, k);
    float ggx2 = geometrySchlickGGX(NdotL, k);
    return ggx1 * ggx2;
}

float fresnelSchlick(vec3 H, vec3 V, float F0) {
    return F0 + (1 - F0) * pow(1 - max(dot(H, V), 0), 5);
}
