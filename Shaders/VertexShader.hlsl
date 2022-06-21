#pragma pack_matrix(row_major)
// an ultra simple hlsl vertex shader
#define MAX_INSTANCE_PER_DRAW 1024
struct OBJ_ATTRIBUTES
{
    float3 Kd; // diffuse reflectivity
    float d; // dissolve (transparency) 
    float3 Ks; // specular reflectivity
    float Ns; // specular exponent
    float3 Ka; // ambient reflectivity
    float sharpness; // local reflection map sharpness
    float3 Tf; // transmission filter
    float Ni; // optical density (index of refraction)
    float3 Ke; // emissive reflectivity
    int illum; // illumination model
};

struct SHADER_MODEL_DATA
{
    float4 lightDirection;
    float4 lightColor;
    float4 ambientColor;
    float4 cameraPos;
    matrix viewMatrix;
    matrix projectionMatrix;
    matrix matrices[MAX_INSTANCE_PER_DRAW];
    OBJ_ATTRIBUTES materials[MAX_INSTANCE_PER_DRAW];
};

//struct VERTEX_SHADER_DATA
//{
//    matrix viewMatrix, projectionMatrix;
//    float4 cameraPos;
//    matrix matrices[MAX_INSTANCE_PER_DRAW]; // world space transforms
//};

[[vk::binding(0, 0)]]
StructuredBuffer<SHADER_MODEL_DATA> SceneData;

//[[vk::binding(0, 0)]]
//StructuredBuffer<VERTEX_SHADER_DATA>SceneData;

[[vk::push_constant]]
cbuffer MESH_INDEX
{
    uint material_offset;
    uint matrix_offset;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 UVW : UVW;
    float3 Normal : NORMAL;
};

struct VS_OUTPUT
{
    float4 posH : SV_POSITION;
    float3 nrmW : NORMAL;
    float3 posW : WORLD;
    float3 uvw : UVW;
};


VS_OUTPUT main(VSInput inputVertex, uint InstanceID : SV_InstanceID) : SV_TARGET
{
    VS_OUTPUT vsOut = (VS_OUTPUT) 0;
    vsOut.posW = mul(inputVertex.Position, SceneData[0].matrices[matrix_offset + InstanceID]);
    vsOut.posH = mul(mul(mul(float4(inputVertex.Position, 1), SceneData[0].matrices[matrix_offset + InstanceID]), SceneData[0].viewMatrix), SceneData[0].projectionMatrix);
    vsOut.nrmW = mul(inputVertex.Normal, SceneData[0].matrices[matrix_offset + InstanceID]);
    vsOut.uvw = inputVertex.UVW;
    return vsOut;
}